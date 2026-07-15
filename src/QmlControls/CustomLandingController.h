/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtPositioning/QGeoCoordinate>

#include <limits>

class Vehicle;
class Fact;
class ParameterManager;
struct CustomLandingCommandContext;

/// Coordinates upload controller for ArduPlane Custom Landing mode.
///
/// Editing the exposed draft properties never sends MAVLink traffic. execute()
/// freezes a snapshot and uploads it as an idempotent three-step transaction:
/// MAV_CMD_SPATIAL_USER_1, MAV_CMD_SPATIAL_USER_2, then MAV_CMD_USER_1.
class CustomLandingController : public QObject
{
    Q_OBJECT
    // QML_ELEMENT
    Q_MOC_INCLUDE("Vehicle.h")

    Q_PROPERTY(Vehicle* vehicle READ vehicle WRITE setVehicle NOTIFY vehicleChanged)

    Q_PROPERTY(QGeoCoordinate loiterCoordinate READ loiterCoordinate WRITE setLoiterCoordinate NOTIFY loiterCoordinateChanged)
    Q_PROPERTY(QGeoCoordinate landingCoordinate READ landingCoordinate WRITE setLandingCoordinate NOTIFY landingCoordinateChanged)
    Q_PROPERTY(double loiterAltitude READ loiterAltitude WRITE setLoiterAltitude NOTIFY loiterAltitudeChanged)
    Q_PROPERTY(double landingAltitude READ landingAltitude WRITE setLandingAltitude NOTIFY landingAltitudeChanged)
    Q_PROPERTY(double loiterRadius READ loiterRadius WRITE setLoiterRadius NOTIFY loiterRadiusChanged)
    Q_PROPERTY(double tangentDistance READ tangentDistance NOTIFY tangentDistanceChanged)
    Q_PROPERTY(double approachAirspeed READ approachAirspeed WRITE setApproachAirspeed NOTIFY approachAirspeedChanged)
    Q_PROPERTY(bool clockwise READ clockwise WRITE setClockwise NOTIFY clockwiseChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool modeActive READ modeActive NOTIFY modeActiveChanged)
    Q_PROPERTY(bool capabilitySupported READ capabilitySupported NOTIFY capabilitySupportedChanged)
    Q_PROPERTY(bool planCommitted READ planCommitted NOTIFY planCommittedChanged)
    Q_PROPERTY(bool canExecute READ canExecute NOTIFY canExecuteChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(quint32 planId READ planId NOTIFY planIdChanged)
    Q_PROPERTY(quint16 planCrc READ planCrc NOTIFY planCrcChanged)

    // Read-only geometry generated when ArduPlane enters FOLLOW_RETURN (mode
    // 92). It is kept separate from the editable CUSTOM_LAND draft above.
    Q_PROPERTY(bool followReturnModeActive READ followReturnModeActive NOTIFY followReturnModeActiveChanged)
    Q_PROPERTY(bool followReturnPlanValid READ followReturnPlanValid NOTIFY followReturnPlanChanged)
    Q_PROPERTY(QGeoCoordinate followReturnEntryCoordinate READ followReturnEntryCoordinate NOTIFY followReturnPlanChanged)
    Q_PROPERTY(QGeoCoordinate followReturnLoiterCoordinate READ followReturnLoiterCoordinate NOTIFY followReturnPlanChanged)
    Q_PROPERTY(QGeoCoordinate followReturnLandingCoordinate READ followReturnLandingCoordinate NOTIFY followReturnPlanChanged)
    Q_PROPERTY(double followReturnLoiterAltitude READ followReturnLoiterAltitude NOTIFY followReturnPlanChanged)
    Q_PROPERTY(double followReturnRadius READ followReturnRadius NOTIFY followReturnPlanChanged)
    Q_PROPERTY(double followReturnTangentDistance READ followReturnTangentDistance NOTIFY followReturnPlanChanged)
    Q_PROPERTY(bool followReturnClockwise READ followReturnClockwise NOTIFY followReturnPlanChanged)
    Q_PROPERTY(bool followReturnSafeClimbRequired READ followReturnSafeClimbRequired NOTIFY followReturnPlanChanged)

public:
    explicit CustomLandingController(QObject* parent = nullptr);
    ~CustomLandingController() override;

    Vehicle* vehicle() const { return _vehicle; }
    void setVehicle(Vehicle* vehicle);

    QGeoCoordinate loiterCoordinate() const { return _loiterCoordinate; }
    void setLoiterCoordinate(const QGeoCoordinate& coordinate);
    QGeoCoordinate landingCoordinate() const { return _landingCoordinate; }
    void setLandingCoordinate(const QGeoCoordinate& coordinate);
    double loiterAltitude() const { return _loiterAltitude; }
    void setLoiterAltitude(double altitude);
    double landingAltitude() const { return _landingAltitude; }
    void setLandingAltitude(double altitude);
    double loiterRadius() const { return _loiterRadius; }
    void setLoiterRadius(double radius);
    double tangentDistance() const { return _tangentDistance; }
    double approachAirspeed() const { return _approachAirspeed; }
    void setApproachAirspeed(double airspeed);
    bool clockwise() const { return _clockwise; }
    void setClockwise(bool clockwise);

    bool busy() const { return _busy; }
    bool modeActive() const { return _modeActive; }
    bool capabilitySupported() const { return _capabilitySupported; }
    bool planCommitted() const { return _planCommitted; }
    bool canExecute() const;
    QString stateText() const { return _stateText; }
    QString errorText() const { return _errorText; }
    quint32 planId() const { return _planId; }
    quint16 planCrc() const { return _planCrc; }

    bool followReturnModeActive() const { return _followReturnModeActive; }
    bool followReturnPlanValid() const { return _followReturnPlanValid; }
    QGeoCoordinate followReturnEntryCoordinate() const { return _followReturnEntryCoordinate; }
    QGeoCoordinate followReturnLoiterCoordinate() const { return _followReturnLoiterCoordinate; }
    QGeoCoordinate followReturnLandingCoordinate() const { return _followReturnLandingCoordinate; }
    double followReturnLoiterAltitude() const { return _followReturnLoiterAltitude; }
    double followReturnRadius() const { return _followReturnRadius; }
    double followReturnTangentDistance() const { return _followReturnTangentDistance; }
    bool followReturnClockwise() const { return _followReturnClockwise; }
    bool followReturnSafeClimbRequired() const { return _followReturnSafeClimbRequired; }

    Q_INVOKABLE void queryCapability();
    Q_INVOKABLE void execute();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void resetDraft();

signals:
    void vehicleChanged();
    void loiterCoordinateChanged();
    void landingCoordinateChanged();
    void loiterAltitudeChanged();
    void landingAltitudeChanged();
    void loiterRadiusChanged();
    void tangentDistanceChanged();
    void approachAirspeedChanged();
    void clockwiseChanged();
    void busyChanged();
    void modeActiveChanged();
    void capabilitySupportedChanged();
    void planCommittedChanged();
    void canExecuteChanged();
    void stateTextChanged();
    void errorTextChanged();
    void planIdChanged();
    void planCrcChanged();
    void followReturnModeActiveChanged();
    void followReturnPlanChanged();

private:
    friend struct CustomLandingCommandContext;

    enum class Operation {
        Idle,
        QueryCapability,
        SendLoiter,
        SendLanding,
        Commit,
        Cancel,
    };

    /// CRC canonical byte stream, in this exact order and little-endian for
    /// multi-byte integers (signed integers use two's complement):
    ///   u8 version, u8 loiter_frame, u8 landing_frame, u8 flags,
    ///   u32 plan_id,
    ///   i32 loiter_lat_e7, i32 loiter_lon_e7, i32 loiter_alt_cm,
    ///   i32 signed_radius_cm, i32 airspeed_cmps,
    ///   i32 landing_lat_e7, i32 landing_lon_e7, i32 landing_alt_cm.
    /// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, refin/refout false,
    /// xorout 0x0000.
    struct PlanSnapshot {
        quint8 version = 1;
        quint8 loiterFrame = 0;
        quint8 landingFrame = 0;
        quint8 flags = 0;
        quint32 planId = 0;
        QGeoCoordinate loiterCoordinate;
        QGeoCoordinate landingCoordinate;
        float loiterAltitude = 0.0F;
        float landingAltitude = 0.0F;
        float signedRadius = 0.0F;
        float approachAirspeed = std::numeric_limits<float>::quiet_NaN();
        qint32 loiterLatE7 = 0;
        qint32 loiterLonE7 = 0;
        qint32 loiterAltCm = 0;
        qint32 signedRadiusCm = 0;
        qint32 airspeedCmps = -1;
        qint32 landingLatE7 = 0;
        qint32 landingLonE7 = 0;
        qint32 landingAltCm = 0;
        quint16 crc = 0;
    };

    void _activeVehicleChanged(Vehicle* activeVehicle);
    void _armedChanged(bool armed);
    void _flightModeChanged();
    void _updateModeActive();
    void _rebuildFollowReturnPlan();
    void _clearFollowReturnPlan();
    double _parameterValue(const QString& name, double fallback) const;
    void _abortOperation(const QString& reason);
    void _setBusy(bool busy);
    void _setCapabilitySupported(bool supported);
    void _setPlanCommitted(bool committed);
    void _setStateText(const QString& text);
    void _setErrorText(const QString& text);
    void _setPlanIdentity(quint32 planId, quint16 crc);
    void _draftChanged();
    void _refreshTangentDistanceParameter();
    void _setTangentDistanceFromVehicle(double distance);
    void _applyPendingTangentDistance();
    double _pointSeparationDistance() const;
    QGeoCoordinate _projectLoiterAtBearing(double bearing) const;
    QGeoCoordinate _constrainLoiterCoordinate(const QGeoCoordinate& coordinate) const;
    bool _validateDraft(QString& error) const;
    PlanSnapshot _snapshotDraft(quint32 planId) const;

    static quint16 _canonicalCrc16(const PlanSnapshot& plan);
    static quint16 _crcAccumulateByte(quint16 crc, quint8 byte);

    void _startOperation(Operation operation);
    void _sendCurrentOperation();
    void _retryOrFail(int failureCode, int ackResult, int resultParam2);
    void _handleCommandResult(Operation operation, quint64 generation, int failureCode, int ackResult, int resultParam2);
    void _finishOperation();
    void _finishWithError(const QString& error);
    QString _operationName(Operation operation) const;
    QString _commandError(Operation operation, int failureCode, int ackResult, int resultParam2) const;

    Vehicle* _vehicle = nullptr;
    QGeoCoordinate _loiterCoordinate;
    QGeoCoordinate _landingCoordinate;
    double _loiterAltitude = 50.0;
    double _landingAltitude = 0.0;
    double _loiterRadius = 100.0;
    double _tangentDistance = 300.0;
    double _pendingTangentDistance = std::numeric_limits<double>::quiet_NaN();
    bool _hasPendingTangentDistance = false;
    double _approachAirspeed = std::numeric_limits<double>::quiet_NaN();
    bool _clockwise = true;

    bool _busy = false;
    bool _modeActive = false;
    bool _capabilitySupported = false;
    bool _planCommitted = false;
    bool _commitUncertain = false;
    QString _stateText;
    QString _errorText;
    quint32 _planId = 0;
    quint16 _planCrc = 0;

    QGeoCoordinate _followReturnLaunchCoordinate;
    QGeoCoordinate _followReturnEntryCoordinate;
    QGeoCoordinate _followReturnLoiterCoordinate;
    QGeoCoordinate _followReturnLandingCoordinate;
    double _followReturnLoiterAltitude = 50.0;
    double _followReturnRadius = 250.0;
    double _followReturnTangentDistance = 300.0;
    bool _followReturnClockwise = true;
    bool _followReturnSafeClimbRequired = false;
    bool _followReturnModeActive = false;
    bool _followReturnPlanValid = false;
    QPointer<Fact> _tangentDistanceFact;
    QPointer<ParameterManager> _parameterManager;

    Operation _operation = Operation::Idle;
    PlanSnapshot _pendingPlan;
    int _attempt = 0;
    int _cancelAction = 0;
    quint64 _operationGeneration = 0;

    static constexpr quint32 kCustomLandingMode = 91;
    static constexpr quint32 kFollowReturnMode = 92;
    static constexpr quint8 kProtocolVersion = 2;
    static constexpr int kMaxAttempts = 3;
    static constexpr int kRetryDelayMs = 250;
    static constexpr double kDefaultTangentDistance = 300.0;
    static constexpr double kMinimumTangentDistance = 30.0;
    static constexpr double kMaximumTangentDistance = 5000.0;
    static constexpr double kGeometryTolerance = 1.0;
};
