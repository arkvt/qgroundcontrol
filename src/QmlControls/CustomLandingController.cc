/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "CustomLandingController.h"

#include "Fact.h"
#include "MAVLinkLib.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

#include <QtCore/QPointer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QTimer>

#include <cmath>
#include <memory>

QGC_LOGGING_CATEGORY(CustomLandingControllerLog, "qgc.qmlcontrols.customlandingcontroller")

namespace {

bool fuzzyEqual(double left, double right)
{
    if (std::isnan(left) && std::isnan(right)) {
        return true;
    }

    return qFuzzyCompare(1.0 + left, 1.0 + right);
}

bool coordinatesNearlyEqual(const QGeoCoordinate& left, const QGeoCoordinate& right)
{
    if (!left.isValid() || !right.isValid()) {
        return left == right;
    }

    const bool altitudeEqual = (std::isnan(left.altitude()) && std::isnan(right.altitude())) ||
                               fuzzyEqual(left.altitude(), right.altitude());
    return altitudeEqual && (left.distanceTo(right) < 0.01);
}

} // namespace

struct CustomLandingCommandContext
{
    QPointer<CustomLandingController> controller;
    QPointer<Vehicle> vehicle;
    int operation = 0;
    quint64 generation = 0;

    static void resultHandler(void* resultHandlerData, int /*compId*/, const mavlink_command_ack_t& ack, Vehicle::MavCmdResultFailureCode_t failureCode)
    {
        const std::unique_ptr<CustomLandingCommandContext> context(static_cast<CustomLandingCommandContext*>(resultHandlerData));
        if (!context->controller || !context->vehicle || context->controller->_vehicle != context->vehicle) {
            return;
        }

        context->controller->_handleCommandResult(
            static_cast<CustomLandingController::Operation>(context->operation),
            context->generation,
            static_cast<int>(failureCode),
            static_cast<int>(ack.result),
            ack.result_param2);
    }
};

CustomLandingController::CustomLandingController(QObject* parent)
    : QObject(parent)
{
    MultiVehicleManager* const manager = MultiVehicleManager::instance();
    connect(manager, &MultiVehicleManager::activeVehicleChanged, this, &CustomLandingController::_activeVehicleChanged);
    setVehicle(manager->activeVehicle());
}

CustomLandingController::~CustomLandingController()
{
    ++_operationGeneration;
}

void CustomLandingController::setVehicle(Vehicle* vehicle)
{
    if (vehicle == _vehicle) {
        return;
    }

    _abortOperation(QString());
    if (_followReturnModeActive) {
        _followReturnModeActive = false;
        emit followReturnModeActiveChanged();
    }
    _clearFollowReturnPlan();
    _followReturnLaunchCoordinate = QGeoCoordinate();
    if (_tangentDistanceFact) {
        disconnect(_tangentDistanceFact, nullptr, this, nullptr);
        _tangentDistanceFact.clear();
    }
    if (_parameterManager) {
        disconnect(_parameterManager, nullptr, this, nullptr);
        _parameterManager.clear();
    }
    if (_vehicle) {
        disconnect(_vehicle, nullptr, this, nullptr);
    }

    _vehicle = vehicle;
    _setCapabilitySupported(false);
    _setPlanCommitted(false);
    _commitUncertain = false;
    _hasPendingTangentDistance = false;
    _setPlanIdentity(0, 0);

    // Never carry a vehicle-specific geometry parameter to another vehicle.
    // A firmware without CLND_TAN_DIST uses the protocol default of 300 m.
    _setTangentDistanceFromVehicle(kDefaultTangentDistance);

    if (_vehicle) {
        connect(_vehicle, &Vehicle::flightModeChanged, this, [this](const QString&) { _flightModeChanged(); });
        connect(_vehicle, &Vehicle::armedChanged, this, &CustomLandingController::_armedChanged);
        connect(_vehicle, &Vehicle::coordinateChanged, this,
                [this](const QGeoCoordinate& coordinate) {
                    // A mode heartbeat can arrive before the first valid
                    // global-position sample. Freeze only that first valid
                    // fallback; an established route must never drift with
                    // later aircraft position updates.
                    if (_followReturnModeActive &&
                        !_followReturnEntryCoordinate.isValid() && coordinate.isValid()) {
                        _followReturnEntryCoordinate = coordinate;
                        _rebuildFollowReturnPlan();
                    }
                });
        connect(_vehicle, &Vehicle::homePositionChanged, this,
                [this](const QGeoCoordinate& homePosition) {
                    if (_vehicle && _vehicle->armed() &&
                        !_followReturnLaunchCoordinate.isValid() && homePosition.isValid()) {
                        _followReturnLaunchCoordinate = homePosition;
                        if (_followReturnModeActive) {
                            _rebuildFollowReturnPlan();
                        }
                    }
                });
        connect(_vehicle, &QObject::destroyed, this, [this]() { setVehicle(nullptr); });

        // When QGC connects after arming, the physical arming coordinate is
        // retained by Vehicle::armedPosition. Home is only a fallback for a
        // connection established after QGC missed the arming transition.
        if (_vehicle->armed()) {
            _followReturnLaunchCoordinate = _vehicle->armedPosition().isValid()
                                                    ? _vehicle->armedPosition()
                                                    : _vehicle->homePosition();
        }

        _parameterManager = _vehicle->parameterManager();
        connect(_parameterManager, &ParameterManager::parametersReadyChanged, this,
                [this](bool parametersReady) {
                    if (parametersReady) {
                        _refreshTangentDistanceParameter();
                        if (_followReturnModeActive) {
                            _rebuildFollowReturnPlan();
                        }
                    }
                });
        connect(_parameterManager, &ParameterManager::factAdded, this,
                [this](int, Fact* fact) {
                    if (fact && fact->name() == QStringLiteral("CLND_TAN_DIST")) {
                        _refreshTangentDistanceParameter();
                    }
                    if (_followReturnModeActive && fact &&
                        (fact->name().startsWith(QStringLiteral("FT_LOSS_")) ||
                         fact->name() == QStringLiteral("FT_DIR"))) {
                        _rebuildFollowReturnPlan();
                    }
                });
        _refreshTangentDistanceParameter();
    }

    _updateModeActive();
    _setErrorText(QString());
    _setStateText(_vehicle ? tr("Query Custom Landing capability") : tr("No active vehicle"));
    emit vehicleChanged();
    emit canExecuteChanged();
}

void CustomLandingController::setLoiterCoordinate(const QGeoCoordinate& coordinate)
{
    if (_busy || _planCommitted || _commitUncertain) {
        return;
    }

    const QGeoCoordinate constrainedCoordinate = _constrainLoiterCoordinate(coordinate);
    if (coordinatesNearlyEqual(constrainedCoordinate, _loiterCoordinate)) {
        return;
    }

    _loiterCoordinate = constrainedCoordinate;
    emit loiterCoordinateChanged();
    _draftChanged();
}

void CustomLandingController::setLandingCoordinate(const QGeoCoordinate& coordinate)
{
    if (_busy || _planCommitted || _commitUncertain) {
        return;
    }

    QGeoCoordinate newCoordinate = coordinate;
    if (newCoordinate.isValid()) {
        newCoordinate.setAltitude(_landingAltitude);
    }

    // The vertical landing point is the geometry anchor. Moving it translates
    // the loiter descent point while preserving the selected approach bearing.
    double loiterBearing = std::numeric_limits<double>::quiet_NaN();
    if (_loiterCoordinate.isValid()) {
        if (_landingCoordinate.isValid()) {
            loiterBearing = _landingCoordinate.azimuthTo(_loiterCoordinate);
        } else if (newCoordinate.isValid() && (newCoordinate.distanceTo(_loiterCoordinate) >= 0.01)) {
            loiterBearing = newCoordinate.azimuthTo(_loiterCoordinate);
        }
    }

    const bool landingChanged = !coordinatesNearlyEqual(newCoordinate, _landingCoordinate);
    if (landingChanged) {
        _landingCoordinate = newCoordinate;
    }

    bool loiterChanged = false;
    if (_landingCoordinate.isValid() && std::isfinite(loiterBearing)) {
        const QGeoCoordinate projectedLoiter = _projectLoiterAtBearing(loiterBearing);
        if (projectedLoiter.isValid() && !coordinatesNearlyEqual(projectedLoiter, _loiterCoordinate)) {
            _loiterCoordinate = projectedLoiter;
            loiterChanged = true;
        }
    }

    if (!landingChanged && !loiterChanged) {
        return;
    }
    if (landingChanged) {
        emit landingCoordinateChanged();
    }
    if (loiterChanged) {
        emit loiterCoordinateChanged();
    }
    _draftChanged();
}

void CustomLandingController::setLoiterAltitude(double altitude)
{
    if (_busy || _planCommitted || _commitUncertain || fuzzyEqual(altitude, _loiterAltitude)) {
        return;
    }

    _loiterAltitude = altitude;
    emit loiterAltitudeChanged();
    _draftChanged();
}

void CustomLandingController::setLandingAltitude(double altitude)
{
    if (_busy || _planCommitted || _commitUncertain || fuzzyEqual(altitude, _landingAltitude)) {
        return;
    }

    _landingAltitude = altitude;
    emit landingAltitudeChanged();
    _draftChanged();
}

void CustomLandingController::setLoiterRadius(double radius)
{
    if (_busy || _planCommitted || _commitUncertain || fuzzyEqual(radius, _loiterRadius)) {
        return;
    }

    const double loiterBearing = (_loiterCoordinate.isValid() && _landingCoordinate.isValid())
        ? _landingCoordinate.azimuthTo(_loiterCoordinate)
        : std::numeric_limits<double>::quiet_NaN();

    _loiterRadius = radius;
    emit loiterRadiusChanged();
    if (std::isfinite(loiterBearing)) {
        const QGeoCoordinate projectedLoiter = _projectLoiterAtBearing(loiterBearing);
        if (projectedLoiter.isValid() && !coordinatesNearlyEqual(projectedLoiter, _loiterCoordinate)) {
            _loiterCoordinate = projectedLoiter;
            emit loiterCoordinateChanged();
        }
    }
    _draftChanged();
}

void CustomLandingController::setApproachAirspeed(double airspeed)
{
    if (_busy || _planCommitted || _commitUncertain || fuzzyEqual(airspeed, _approachAirspeed)) {
        return;
    }

    _approachAirspeed = airspeed;
    emit approachAirspeedChanged();
    _draftChanged();
}

void CustomLandingController::setClockwise(bool clockwise)
{
    if (_busy || _planCommitted || _commitUncertain || clockwise == _clockwise) {
        return;
    }

    _clockwise = clockwise;
    emit clockwiseChanged();
    _draftChanged();
}

void CustomLandingController::_refreshTangentDistanceParameter()
{
    if (!_vehicle || !_parameterManager) {
        return;
    }

    if (!_parameterManager->parameterExists(ParameterManager::defaultComponentId,
                                            QStringLiteral("CLND_TAN_DIST"))) {
        if (_parameterManager->parametersReady()) {
            _setTangentDistanceFromVehicle(kDefaultTangentDistance);
        }
        return;
    }

    Fact* const fact = _parameterManager->getParameter(ParameterManager::defaultComponentId,
                                                       QStringLiteral("CLND_TAN_DIST"));
    if (_tangentDistanceFact != fact) {
        if (_tangentDistanceFact) {
            disconnect(_tangentDistanceFact, nullptr, this, nullptr);
        }
        _tangentDistanceFact = fact;
        connect(fact, &Fact::rawValueChanged, this, [this](const QVariant& value) {
            bool conversionOk = false;
            const double distance = value.toDouble(&conversionOk);
            _setTangentDistanceFromVehicle(conversionOk
                                               ? distance
                                               : std::numeric_limits<double>::quiet_NaN());
        });
    }

    bool conversionOk = false;
    const double distance = fact->rawValue().toDouble(&conversionOk);
    _setTangentDistanceFromVehicle(conversionOk
                                       ? distance
                                       : std::numeric_limits<double>::quiet_NaN());
}

void CustomLandingController::_setTangentDistanceFromVehicle(double distance)
{
    if (_busy || _planCommitted || _commitUncertain) {
        _pendingTangentDistance = distance;
        _hasPendingTangentDistance = true;
        return;
    }

    _hasPendingTangentDistance = false;
    if (fuzzyEqual(distance, _tangentDistance)) {
        return;
    }

    const double loiterBearing = (_loiterCoordinate.isValid() && _landingCoordinate.isValid())
        ? _landingCoordinate.azimuthTo(_loiterCoordinate)
        : std::numeric_limits<double>::quiet_NaN();

    _tangentDistance = distance;
    emit tangentDistanceChanged();

    if (std::isfinite(loiterBearing)) {
        const QGeoCoordinate projectedLoiter = _projectLoiterAtBearing(loiterBearing);
        if (projectedLoiter.isValid() && !coordinatesNearlyEqual(projectedLoiter, _loiterCoordinate)) {
            _loiterCoordinate = projectedLoiter;
            emit loiterCoordinateChanged();
        }
    }
    _draftChanged();
}

void CustomLandingController::_applyPendingTangentDistance()
{
    if (!_hasPendingTangentDistance || _busy || _planCommitted || _commitUncertain) {
        return;
    }

    const double distance = _pendingTangentDistance;
    _hasPendingTangentDistance = false;
    _setTangentDistanceFromVehicle(distance);
}

double CustomLandingController::_pointSeparationDistance() const
{
    if (!std::isfinite(_loiterRadius) || (_loiterRadius <= 0.0) ||
        !std::isfinite(_tangentDistance) || (_tangentDistance <= 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::hypot(_loiterRadius, _tangentDistance);
}

QGeoCoordinate CustomLandingController::_projectLoiterAtBearing(double bearing) const
{
    const double separationDistance = _pointSeparationDistance();
    if (!_landingCoordinate.isValid() || !std::isfinite(bearing) || !std::isfinite(separationDistance)) {
        return QGeoCoordinate();
    }

    QGeoCoordinate projected = _landingCoordinate.atDistanceAndAzimuth(separationDistance, bearing);
    projected.setAltitude(_loiterAltitude);
    return projected;
}

QGeoCoordinate CustomLandingController::_constrainLoiterCoordinate(const QGeoCoordinate& coordinate) const
{
    QGeoCoordinate constrainedCoordinate = coordinate;
    if (constrainedCoordinate.isValid()) {
        constrainedCoordinate.setAltitude(_loiterAltitude);
    }
    if (!constrainedCoordinate.isValid() || !_landingCoordinate.isValid()) {
        return constrainedCoordinate;
    }

    // At the exact landing point the bearing is undefined. Retain the current
    // radial direction instead of making the loiter marker jump north.
    if ((_landingCoordinate.distanceTo(constrainedCoordinate) < 0.01) && _loiterCoordinate.isValid()) {
        return _loiterCoordinate;
    }

    const QGeoCoordinate projected = _projectLoiterAtBearing(_landingCoordinate.azimuthTo(constrainedCoordinate));
    return projected.isValid() ? projected : constrainedCoordinate;
}

bool CustomLandingController::canExecute() const
{
    QString error;
    return _validateDraft(error);
}

void CustomLandingController::queryCapability()
{
    if (_busy) {
        if (_operation == Operation::QueryCapability) {
            return;
        }
        _setErrorText(tr("Another Custom Landing command is in progress"));
        return;
    }
    if (!_vehicle) {
        _finishWithError(tr("No active vehicle"));
        return;
    }

    _setErrorText(QString());
    _startOperation(Operation::QueryCapability);
}

void CustomLandingController::execute()
{
    if (_busy) {
        _setErrorText(tr("Another Custom Landing command is in progress"));
        return;
    }

    QString validationError;
    if (!_validateDraft(validationError)) {
        _finishWithError(validationError);
        return;
    }

    quint32 planId = 0;
    do {
        planId = QRandomGenerator::global()->generate() & 0x00FFFFFFU;
    } while ((planId == 0) || (planId == _planId));

    _pendingPlan = _snapshotDraft(planId);
    _commitUncertain = false;
    _setPlanIdentity(_pendingPlan.planId, _pendingPlan.crc);
    _setErrorText(QString());
    _startOperation(Operation::SendLoiter);
}

void CustomLandingController::cancel()
{
    if (!_vehicle) {
        _finishWithError(tr("No active vehicle"));
        return;
    }

    const bool commitMayHaveReachedVehicle = _planCommitted || _commitUncertain || (_operation == Operation::Commit);
    const quint32 planId = _planId;
    _abortOperation(QString());

    if (!_modeActive) {
        _setPlanCommitted(false);
        _commitUncertain = false;
        _setPlanIdentity(0, 0);
        _setErrorText(QString());
        _setStateText(tr("Custom Landing mode is not active"));
        return;
    }

    _cancelAction = commitMayHaveReachedVehicle ? 1 : 0;
    _pendingPlan.planId = planId;
    _setErrorText(QString());
    _startOperation(Operation::Cancel);
}

void CustomLandingController::resetDraft()
{
    if (_busy) {
        _setErrorText(tr("Cannot reset while a command is in progress"));
        return;
    }
    if (_planCommitted || _commitUncertain) {
        _setErrorText(tr("Cancel the onboard Custom Landing plan before resetting the draft"));
        return;
    }

    const bool loiterChanged = _loiterCoordinate.isValid();
    const bool landingChanged = _landingCoordinate.isValid();
    const bool loiterAltitudeChangedValue = !fuzzyEqual(_loiterAltitude, 50.0);
    const bool landingAltitudeChangedValue = !fuzzyEqual(_landingAltitude, 0.0);
    const bool radiusChanged = !fuzzyEqual(_loiterRadius, 100.0);
    const bool airspeedChanged = !std::isnan(_approachAirspeed);
    const bool clockwiseChangedValue = !_clockwise;

    _loiterCoordinate = QGeoCoordinate();
    _landingCoordinate = QGeoCoordinate();
    _loiterAltitude = 50.0;
    _landingAltitude = 0.0;
    _loiterRadius = 100.0;
    _approachAirspeed = std::numeric_limits<double>::quiet_NaN();
    _clockwise = true;
    _setPlanIdentity(0, 0);
    _setErrorText(QString());
    _setStateText(_modeActive ? tr("Select loiter and landing points") : tr("Custom Landing mode is not active"));

    if (loiterChanged) {
        emit loiterCoordinateChanged();
    }
    if (landingChanged) {
        emit landingCoordinateChanged();
    }
    if (loiterAltitudeChangedValue) {
        emit loiterAltitudeChanged();
    }
    if (landingAltitudeChangedValue) {
        emit landingAltitudeChanged();
    }
    if (radiusChanged) {
        emit loiterRadiusChanged();
    }
    if (airspeedChanged) {
        emit approachAirspeedChanged();
    }
    if (clockwiseChangedValue) {
        emit clockwiseChanged();
    }
    emit canExecuteChanged();
}

void CustomLandingController::_activeVehicleChanged(Vehicle* activeVehicle)
{
    setVehicle(activeVehicle);
}

void CustomLandingController::_armedChanged(bool armed)
{
    if (!armed) {
        _followReturnLaunchCoordinate = QGeoCoordinate();
        return;
    }

    // FOLLOW_RETURN freezes current_loc on this same arming transition.
    // Prefer the live coordinate here; Home remains the late-connect fallback
    // installed by setVehicle().
    if (_vehicle && _vehicle->armedPosition().isValid()) {
        _followReturnLaunchCoordinate = _vehicle->armedPosition();
    } else if (_vehicle && _vehicle->coordinate().isValid()) {
        _followReturnLaunchCoordinate = _vehicle->coordinate();
    } else if (_vehicle && _vehicle->homePosition().isValid()) {
        _followReturnLaunchCoordinate = _vehicle->homePosition();
    }
}

double CustomLandingController::_parameterValue(const QString& name, double fallback) const
{
    if (!_parameterManager ||
        !_parameterManager->parameterExists(ParameterManager::defaultComponentId, name)) {
        return fallback;
    }

    Fact* const fact = _parameterManager->getParameter(ParameterManager::defaultComponentId, name);
    bool ok = false;
    const double value = fact ? fact->rawValue().toDouble(&ok) : fallback;
    return ok && std::isfinite(value) ? value : fallback;
}

void CustomLandingController::_clearFollowReturnPlan()
{
    const bool changed = _followReturnPlanValid ||
                         _followReturnEntryCoordinate.isValid() ||
                         _followReturnLoiterCoordinate.isValid() ||
                         _followReturnLandingCoordinate.isValid();

    _followReturnPlanValid = false;
    _followReturnEntryCoordinate = QGeoCoordinate();
    _followReturnLoiterCoordinate = QGeoCoordinate();
    _followReturnLandingCoordinate = QGeoCoordinate();
    _followReturnLoiterAltitude = 50.0;
    _followReturnRadius = 250.0;
    _followReturnTangentDistance = 300.0;
    _followReturnClockwise = true;
    _followReturnSafeClimbRequired = false;

    if (changed) {
        emit followReturnPlanChanged();
    }
}

void CustomLandingController::_rebuildFollowReturnPlan()
{
    _followReturnPlanValid = false;
    _followReturnLoiterCoordinate = QGeoCoordinate();
    _followReturnLandingCoordinate = QGeoCoordinate();
    _followReturnSafeClimbRequired = false;

    if (!_followReturnModeActive || !_vehicle ||
        !_followReturnEntryCoordinate.isValid()) {
        emit followReturnPlanChanged();
        return;
    }

    QGeoCoordinate landing = _followReturnLaunchCoordinate;
    if (!landing.isValid()) {
        landing = _vehicle->homePosition();
    }

    double radius = _parameterValue(QStringLiteral("FT_LOSS_RAD"), 250.0);
    // Match the flight controller's compatibility handling for a persisted
    // zero from the development version of mode 92.
    if (qFuzzyIsNull(radius)) {
        radius = 250.0;
    }
    const double tangentDistance = _parameterValue(QStringLiteral("FT_LOSS_TAN_DST"), 300.0);
    const double descentAltitude = _parameterValue(QStringLiteral("FT_LOSS_DSC_ALT"), 50.0);
    const double returnAltitude = _parameterValue(QStringLiteral("FT_LOSS_RET_ALT"), 80.0);
    const double direction = _parameterValue(QStringLiteral("FT_DIR"), 0.0);

    if (!landing.isValid() ||
        !std::isfinite(radius) || radius < 10.0 || radius > 1000.0 ||
        !std::isfinite(tangentDistance) || tangentDistance < 30.0 || tangentDistance > 5000.0 ||
        !std::isfinite(descentAltitude) || descentAltitude < 20.0 || descentAltitude > 500.0 ||
        !std::isfinite(returnAltitude) || returnAltitude < 20.0 || returnAltitude > 1000.0 ||
        returnAltitude < descentAltitude + 20.0) {
        emit followReturnPlanChanged();
        return;
    }

    const double launchToEntryBearing = landing.azimuthTo(_followReturnEntryCoordinate);
    const double centerDistance = std::hypot(radius, tangentDistance);
    if (!std::isfinite(launchToEntryBearing) || !std::isfinite(centerDistance)) {
        emit followReturnPlanChanged();
        return;
    }

    QGeoCoordinate loiter = landing.atDistanceAndAzimuth(centerDistance, launchToEntryBearing);
    if (!loiter.isValid()) {
        emit followReturnPlanChanged();
        return;
    }
    if (std::isfinite(landing.altitude())) {
        loiter.setAltitude(landing.altitude() + descentAltitude);
    }

    _followReturnLandingCoordinate = landing;
    _followReturnLoiterCoordinate = loiter;
    _followReturnLoiterAltitude = descentAltitude;
    _followReturnRadius = radius;
    _followReturnTangentDistance = tangentDistance;
    _followReturnClockwise = std::lround(direction) == 0;
    _followReturnSafeClimbRequired =
            std::isfinite(_followReturnEntryCoordinate.altitude()) &&
            std::isfinite(landing.altitude()) &&
            _followReturnEntryCoordinate.altitude() < landing.altitude() + returnAltitude - 5.0;
    _followReturnPlanValid = true;
    emit followReturnPlanChanged();
}

void CustomLandingController::_flightModeChanged()
{
    _updateModeActive();
}

void CustomLandingController::_updateModeActive()
{
    const bool customLandingActive = _vehicle && (_vehicle->customMode() == kCustomLandingMode);
    if (customLandingActive != _modeActive) {
        _modeActive = customLandingActive;
        if (!_modeActive) {
            if (_busy && (_operation != Operation::QueryCapability)) {
                _abortOperation(tr("Vehicle left Custom Landing mode"));
            }
            _setPlanCommitted(false);
            _commitUncertain = false;
            _applyPendingTangentDistance();
            _setStateText(tr("Custom Landing mode is not active"));
        } else {
            _setPlanCommitted(false);
            _commitUncertain = false;
            _setStateText(tr("Select loiter and landing points"));
        }

        emit modeActiveChanged();
        emit canExecuteChanged();
    }

    const bool followReturnActive = _vehicle && (_vehicle->customMode() == kFollowReturnMode);
    if (followReturnActive == _followReturnModeActive) {
        return;
    }

    _followReturnModeActive = followReturnActive;
    if (_followReturnModeActive) {
        // Freeze the loss-return entry point. Continuing coordinate updates
        // must move only the aircraft icon, never the displayed route.
        _followReturnEntryCoordinate = _vehicle->coordinate();
        _rebuildFollowReturnPlan();
    } else {
        _clearFollowReturnPlan();
    }
    emit followReturnModeActiveChanged();
}

void CustomLandingController::_abortOperation(const QString& reason)
{
    ++_operationGeneration;
    _operation = Operation::Idle;
    _attempt = 0;
    _setBusy(false);
    if (!reason.isEmpty()) {
        _setErrorText(reason);
        _setStateText(reason);
    }
}

void CustomLandingController::_setBusy(bool busy)
{
    if (busy == _busy) {
        return;
    }

    _busy = busy;
    emit busyChanged();
    emit canExecuteChanged();
}

void CustomLandingController::_setCapabilitySupported(bool supported)
{
    if (supported == _capabilitySupported) {
        return;
    }

    _capabilitySupported = supported;
    emit capabilitySupportedChanged();
    emit canExecuteChanged();
}

void CustomLandingController::_setPlanCommitted(bool committed)
{
    if (committed == _planCommitted) {
        return;
    }

    _planCommitted = committed;
    emit planCommittedChanged();
    emit canExecuteChanged();
}

void CustomLandingController::_setStateText(const QString& text)
{
    if (text == _stateText) {
        return;
    }

    _stateText = text;
    emit stateTextChanged();
}

void CustomLandingController::_setErrorText(const QString& text)
{
    if (text == _errorText) {
        return;
    }

    _errorText = text;
    emit errorTextChanged();
}

void CustomLandingController::_setPlanIdentity(quint32 planId, quint16 crc)
{
    if (planId != _planId) {
        _planId = planId;
        emit planIdChanged();
    }
    if (crc != _planCrc) {
        _planCrc = crc;
        emit planCrcChanged();
    }
}

void CustomLandingController::_draftChanged()
{
    _setPlanIdentity(0, 0);
    _setErrorText(QString());
    _setStateText(_modeActive ? tr("Custom Landing draft changed") : tr("Custom Landing mode is not active"));
    emit canExecuteChanged();
}

bool CustomLandingController::_validateDraft(QString& error) const
{
    if (!_vehicle) {
        error = tr("No active vehicle");
        return false;
    }
    if (!_capabilitySupported) {
        error = tr("Vehicle has not confirmed Custom Landing capability");
        return false;
    }
    if (!_modeActive) {
        error = tr("Vehicle is not in Custom Landing mode");
        return false;
    }
    if (_busy) {
        error = tr("Another Custom Landing command is in progress");
        return false;
    }
    if (_planCommitted) {
        error = tr("A Custom Landing plan is already committed");
        return false;
    }
    if (_commitUncertain) {
        error = tr("Commit status is uncertain; cancel the onboard plan before retrying");
        return false;
    }
    if (!_loiterCoordinate.isValid() || !_landingCoordinate.isValid()) {
        error = tr("Select valid loiter and landing coordinates");
        return false;
    }
    if ((qFuzzyIsNull(_loiterCoordinate.latitude()) && qFuzzyIsNull(_loiterCoordinate.longitude())) ||
        (qFuzzyIsNull(_landingCoordinate.latitude()) && qFuzzyIsNull(_landingCoordinate.longitude()))) {
        error = tr("Latitude and longitude cannot both be zero");
        return false;
    }
    if (!std::isfinite(_loiterAltitude) || (_loiterAltitude <= 0.0) ||
        !std::isfinite(_landingAltitude) || !std::isfinite(_loiterRadius) ||
        (_loiterRadius < 10.0) || (_loiterRadius > 10000.0) ||
        !std::isfinite(_tangentDistance) ||
        (_tangentDistance < kMinimumTangentDistance) ||
        (_tangentDistance > kMaximumTangentDistance)) {
        error = tr("Altitude, loiter radius, or CLND_TAN_DIST is invalid");
        return false;
    }
    if ((_loiterAltitude - _landingAltitude) < 20.0) {
        error = tr("Loiter altitude must be at least 20 metres above landing altitude");
        return false;
    }
    constexpr double maxCanonicalValue = static_cast<double>(std::numeric_limits<qint32>::max()) / 100.0;
    if ((std::abs(_loiterAltitude) > maxCanonicalValue) ||
        (std::abs(_landingAltitude) > maxCanonicalValue) ||
        (std::abs(_loiterRadius) > maxCanonicalValue) ||
        (!std::isnan(_approachAirspeed) && !std::isfinite(_approachAirspeed)) ||
        (std::isfinite(_approachAirspeed) && (std::abs(_approachAirspeed) > maxCanonicalValue))) {
        error = tr("A Custom Landing numeric value is out of range");
        return false;
    }
    const double expectedCenterDistance = _pointSeparationDistance();
    const double actualCenterDistance = _loiterCoordinate.distanceTo(_landingCoordinate);
    if (!std::isfinite(expectedCenterDistance) || !std::isfinite(actualCenterDistance) ||
        (std::abs(actualCenterDistance - expectedCenterDistance) > kGeometryTolerance)) {
        error = tr("Loiter descent point does not match the fixed CLND_TAN_DIST geometry");
        return false;
    }

    return true;
}

CustomLandingController::PlanSnapshot CustomLandingController::_snapshotDraft(quint32 planId) const
{
    // Canonical centimetres are computed from the exact float placed on the
    // MAVLink wire, using float multiplication just like ArduPlane. Promoting
    // to double before multiplying can differ by 1 cm at half-way values.
    const auto wireFloatToCentimetres = [](float value) {
        return static_cast<qint32>(std::lround(value * 100.0F));
    };

    PlanSnapshot plan;
    plan.version = kProtocolVersion;
    plan.loiterFrame = static_cast<quint8>(MAV_FRAME_GLOBAL_RELATIVE_ALT_INT);
    plan.landingFrame = static_cast<quint8>(MAV_FRAME_GLOBAL_RELATIVE_ALT_INT);
    plan.flags = 0;
    plan.planId = planId;
    plan.loiterCoordinate = _loiterCoordinate;
    plan.landingCoordinate = _landingCoordinate;
    plan.loiterAltitude = static_cast<float>(_loiterAltitude);
    plan.landingAltitude = static_cast<float>(_landingAltitude);
    plan.signedRadius = static_cast<float>(_clockwise ? _loiterRadius : -_loiterRadius);
    plan.approachAirspeed = static_cast<float>(_approachAirspeed);

    // Vehicle::sendMavCommandIntWithHandler encodes x/y with the same
    // double-to-int32 truncation. CRC must describe the exact wire values.
    plan.loiterLatE7 = static_cast<qint32>(plan.loiterCoordinate.latitude() * 1e7);
    plan.loiterLonE7 = static_cast<qint32>(plan.loiterCoordinate.longitude() * 1e7);
    plan.loiterAltCm = wireFloatToCentimetres(plan.loiterAltitude);
    plan.signedRadiusCm = wireFloatToCentimetres(plan.signedRadius);
    plan.airspeedCmps = (!std::isfinite(plan.approachAirspeed) || (plan.approachAirspeed <= 0.0F))
                            ? -1
                            : wireFloatToCentimetres(plan.approachAirspeed);
    plan.landingLatE7 = static_cast<qint32>(plan.landingCoordinate.latitude() * 1e7);
    plan.landingLonE7 = static_cast<qint32>(plan.landingCoordinate.longitude() * 1e7);
    plan.landingAltCm = wireFloatToCentimetres(plan.landingAltitude);
    plan.crc = _canonicalCrc16(plan);
    return plan;
}

quint16 CustomLandingController::_canonicalCrc16(const PlanSnapshot& plan)
{
    quint16 crc = 0xFFFFU;
    const auto appendU8 = [&crc](quint8 value) { crc = _crcAccumulateByte(crc, value); };
    const auto appendU32 = [&crc](quint32 value) {
        for (int byteIndex = 0; byteIndex < 4; ++byteIndex) {
            crc = _crcAccumulateByte(crc, static_cast<quint8>((value >> (byteIndex * 8)) & 0xFFU));
        }
    };
    const auto appendI32 = [&appendU32](qint32 value) { appendU32(static_cast<quint32>(value)); };

    appendU8(plan.version);
    appendU8(plan.loiterFrame);
    appendU8(plan.landingFrame);
    appendU8(plan.flags);
    appendU32(plan.planId);
    appendI32(plan.loiterLatE7);
    appendI32(plan.loiterLonE7);
    appendI32(plan.loiterAltCm);
    appendI32(plan.signedRadiusCm);
    appendI32(plan.airspeedCmps);
    appendI32(plan.landingLatE7);
    appendI32(plan.landingLonE7);
    appendI32(plan.landingAltCm);
    return crc;
}

quint16 CustomLandingController::_crcAccumulateByte(quint16 crc, quint8 byte)
{
    crc ^= static_cast<quint16>(byte) << 8;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000U) ? static_cast<quint16>((crc << 1) ^ 0x1021U) : static_cast<quint16>(crc << 1);
    }
    return crc;
}

void CustomLandingController::_startOperation(Operation operation)
{
    _operation = operation;
    _attempt = 0;
    ++_operationGeneration;
    _setBusy(true);
    _sendCurrentOperation();
}

void CustomLandingController::_sendCurrentOperation()
{
    if (!_vehicle) {
        _finishWithError(tr("No active vehicle"));
        return;
    }
    if ((_operation != Operation::QueryCapability) && !_modeActive) {
        _finishWithError(tr("Vehicle is not in Custom Landing mode"));
        return;
    }

    ++_attempt;
    switch (_operation) {
    case Operation::QueryCapability:
        _setStateText(tr("Querying Custom Landing capability (%1/%2)").arg(_attempt).arg(kMaxAttempts));
        break;
    case Operation::SendLoiter:
        _setStateText(tr("Sending loiter point (%1/%2)").arg(_attempt).arg(kMaxAttempts));
        break;
    case Operation::SendLanding:
        _setStateText(tr("Sending landing point (%1/%2)").arg(_attempt).arg(kMaxAttempts));
        break;
    case Operation::Commit:
        _setStateText(tr("Committing Custom Landing plan (%1/%2)").arg(_attempt).arg(kMaxAttempts));
        break;
    case Operation::Cancel:
        _setStateText(tr("Cancelling Custom Landing plan (%1/%2)").arg(_attempt).arg(kMaxAttempts));
        break;
    case Operation::Idle:
        return;
    }

    if (!_vehicle->vehicleLinkManager()->primaryLink().lock()) {
        _retryOrFail(static_cast<int>(Vehicle::MavCmdResultFailureNoResponseToCommand), MAV_RESULT_FAILED, 0);
        return;
    }

    auto* const context = new CustomLandingCommandContext {
        this,
        _vehicle,
        static_cast<int>(_operation),
        _operationGeneration,
    };
    Vehicle::MavCmdAckHandlerInfo_t handlerInfo = {};
    handlerInfo.resultHandler = CustomLandingCommandContext::resultHandler;
    handlerInfo.resultHandlerData = context;

    switch (_operation) {
    case Operation::QueryCapability:
        _vehicle->sendMavCommandWithHandler(
            &handlerInfo, _vehicle->defaultComponentId(), MAV_CMD_USER_3,
            static_cast<float>(kProtocolVersion));
        break;
    case Operation::SendLoiter:
        _vehicle->sendMavCommandIntWithHandler(
            &handlerInfo, _vehicle->defaultComponentId(), MAV_CMD_SPATIAL_USER_1,
            static_cast<MAV_FRAME>(_pendingPlan.loiterFrame),
            static_cast<float>(_pendingPlan.planId), _pendingPlan.signedRadius, _pendingPlan.approachAirspeed, static_cast<float>(_pendingPlan.flags),
            _pendingPlan.loiterCoordinate.latitude(), _pendingPlan.loiterCoordinate.longitude(), _pendingPlan.loiterAltitude);
        break;
    case Operation::SendLanding:
        _vehicle->sendMavCommandIntWithHandler(
            &handlerInfo, _vehicle->defaultComponentId(), MAV_CMD_SPATIAL_USER_2,
            static_cast<MAV_FRAME>(_pendingPlan.landingFrame),
            static_cast<float>(_pendingPlan.planId), 0.0F, 0.0F, 0.0F,
            _pendingPlan.landingCoordinate.latitude(), _pendingPlan.landingCoordinate.longitude(), _pendingPlan.landingAltitude);
        break;
    case Operation::Commit:
        _vehicle->sendMavCommandWithHandler(
            &handlerInfo, _vehicle->defaultComponentId(), MAV_CMD_USER_1,
            static_cast<float>(_pendingPlan.planId), static_cast<float>(_pendingPlan.crc), static_cast<float>(kProtocolVersion));
        break;
    case Operation::Cancel:
        _vehicle->sendMavCommandWithHandler(
            &handlerInfo, _vehicle->defaultComponentId(), MAV_CMD_USER_2,
            static_cast<float>(_pendingPlan.planId), static_cast<float>(_cancelAction));
        break;
    case Operation::Idle:
        delete context;
        break;
    }
}

void CustomLandingController::_retryOrFail(int failureCode, int ackResult, int resultParam2)
{
    const bool transportFailure = (failureCode == static_cast<int>(Vehicle::MavCmdResultFailureNoResponseToCommand)) ||
                                  (failureCode == static_cast<int>(Vehicle::MavCmdResultFailureDuplicateCommand));
    if ((_operation == Operation::Commit) && transportFailure) {
        // Once any COMMIT attempt may have reached the aircraft, only an
        // ACCEPTED COMMIT or an ACCEPTED CANCEL can clear the uncertainty.
        _commitUncertain = true;
    }
    if (transportFailure && (_attempt < kMaxAttempts)) {
        const quint64 generation = _operationGeneration;
        const Operation operation = _operation;
        _setStateText(tr("No response for %1; retrying").arg(_operationName(operation)));
        QTimer::singleShot(kRetryDelayMs, this, [this, generation, operation]() {
            if (_busy && (generation == _operationGeneration) && (operation == _operation)) {
                _sendCurrentOperation();
            }
        });
        return;
    }

    if (_operation == Operation::QueryCapability) {
        _setCapabilitySupported(false);
    }
    _finishWithError(_commandError(_operation, failureCode, ackResult, resultParam2));
}

void CustomLandingController::_handleCommandResult(Operation operation, quint64 generation, int failureCode, int ackResult, int resultParam2)
{
    if (!_busy || (generation != _operationGeneration) || (operation != _operation)) {
        return;
    }

    const bool accepted = (failureCode == static_cast<int>(Vehicle::MavCmdResultCommandResultOnly)) &&
                          (ackResult == MAV_RESULT_ACCEPTED);
    if (!accepted) {
        _retryOrFail(failureCode, ackResult, resultParam2);
        return;
    }

    switch (_operation) {
    case Operation::QueryCapability:
        _setCapabilitySupported(true);
        _setErrorText(QString());
        _setStateText(tr("Custom Landing protocol version %1 supported").arg(kProtocolVersion));
        _finishOperation();
        break;
    case Operation::SendLoiter:
        _operation = Operation::SendLanding;
        _attempt = 0;
        _sendCurrentOperation();
        break;
    case Operation::SendLanding:
        _operation = Operation::Commit;
        _attempt = 0;
        _sendCurrentOperation();
        break;
    case Operation::Commit:
        _commitUncertain = false;
        _setPlanCommitted(true);
        _setErrorText(QString());
        _setStateText(tr("Custom Landing plan committed; execution accepted"));
        _finishOperation();
        break;
    case Operation::Cancel:
        _commitUncertain = false;
        _setPlanCommitted(false);
        _setPlanIdentity(0, 0);
        _setErrorText(QString());
        _setStateText(tr("Custom Landing plan cancelled; vehicle holding"));
        _finishOperation();
        break;
    case Operation::Idle:
        break;
    }
}

void CustomLandingController::_finishOperation()
{
    _operation = Operation::Idle;
    _attempt = 0;
    _setBusy(false);
    _applyPendingTangentDistance();
}

void CustomLandingController::_finishWithError(const QString& error)
{
    _operation = Operation::Idle;
    _attempt = 0;
    _setErrorText(error);
    _setStateText(error);
    _setBusy(false);
    _applyPendingTangentDistance();
}

QString CustomLandingController::_commandError(Operation operation, int failureCode, int ackResult, int resultParam2) const
{
    QString detail;
    if (failureCode == static_cast<int>(Vehicle::MavCmdResultFailureNoResponseToCommand)) {
        detail = tr("no COMMAND_ACK received");
    } else if (failureCode == static_cast<int>(Vehicle::MavCmdResultFailureDuplicateCommand)) {
        detail = tr("another command with the same MAV_CMD is pending");
    } else {
        switch (ackResult) {
        case MAV_RESULT_TEMPORARILY_REJECTED:
            detail = tr("temporarily rejected");
            break;
        case MAV_RESULT_DENIED:
            detail = tr("denied by vehicle safety validation");
            break;
        case MAV_RESULT_UNSUPPORTED:
            detail = tr("unsupported by vehicle");
            break;
        case MAV_RESULT_FAILED:
            detail = tr("vehicle reported failure");
            break;
        default:
            detail = tr("MAV_RESULT %1").arg(ackResult);
            break;
        }
    }

    if (resultParam2 != 0) {
        detail += tr(" (reason %1)").arg(resultParam2);
    }
    if ((operation == Operation::Commit) && _commitUncertain) {
        detail += tr("; commit status is uncertain, use Cancel before retrying");
    }
    if ((operation == Operation::Cancel) && (ackResult == MAV_RESULT_DENIED)) {
        detail += tr("; after VTOL approach starts, explicitly switch to QLOITER or QRTL if an abort is required");
    }

    return tr("Custom Landing %1 failed: %2").arg(_operationName(operation), detail);
}

QString CustomLandingController::_operationName(Operation operation) const
{
    switch (operation) {
    case Operation::QueryCapability:
        return tr("capability query");
    case Operation::SendLoiter:
        return tr("loiter point");
    case Operation::SendLanding:
        return tr("landing point");
    case Operation::Commit:
        return tr("commit");
    case Operation::Cancel:
        return tr("cancel");
    case Operation::Idle:
    default:
        return tr("command");
    }
}
