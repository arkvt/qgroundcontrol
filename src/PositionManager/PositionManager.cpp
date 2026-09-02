/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PositionManager.h"
#include "QGCApplication.h"
#include "QGCCorePlugin.h"
#include "SimulatedPosition.h"
// #include "DeviceInfo.h"
#include "QGCLoggingCategory.h"

#include <QtCore/qapplicationstatic.h>
#include <QtCore/QPermissions>
#include <QtPositioning/QGeoPositionInfoSource>
#include <QtPositioning/private/qgeopositioninfosource_p.h>
#include <QtPositioning/QNmeaPositionInfoSource>
#include <QtQml/qqml.h>

namespace {
constexpr int kMaxNmeaRawDataBytes = 128 * 1024;

} // namespace

QGC_LOGGING_CATEGORY(QGCPositionManagerLog, "qgc.positionmanager.positionmanager")

Q_APPLICATION_STATIC(QGCPositionManager, _positionManager);

QGCPositionManager::QGCPositionManager(QObject *parent)
    : QObject(parent)
{
    // qCDebug(QGCPositionManagerLog) << Q_FUNC_INFO << this;

    _nmeaRawDataNotifyTimer.setSingleShot(true);
    _nmeaRawDataNotifyTimer.setInterval(100);
    (void) connect(&_nmeaRawDataNotifyTimer, &QTimer::timeout, this, &QGCPositionManager::nmeaRawDataChanged);
}

QGCPositionManager::~QGCPositionManager()
{
    // qCDebug(QGCPositionManagerLog) << Q_FUNC_INFO << this;
}

QGCPositionManager *QGCPositionManager::instance()
{
    return _positionManager();
}

void QGCPositionManager::registerQmlTypes()
{
    (void) qmlRegisterUncreatableType<QGCPositionManager>("QGroundControl.QGCPositionManager", 1, 0, "QGCPositionManager", "Reference only");
}

void QGCPositionManager::init()
{
    if (qgcApp()->runningUnitTests()) {
        _simulatedSource = new SimulatedPosition(this);
        _setPositionSource(QGCPositionSource::Simulated);
    } else {
        _checkPermission();
    }
}

void QGCPositionManager::_setupPositionSources()
{
    _defaultSource = QGCCorePlugin::instance()->createPositionSource(this);
    if (_defaultSource) {
        _usingPluginSource = true;
    } else {
        const QStringList availableSources = QGeoPositionInfoSource::availableSources();
        qCDebug(QGCPositionManagerLog) << Q_FUNC_INFO << availableSources;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        _defaultSource = QGeoPositionInfoSource::createDefaultSource(this);
#else
        // The Qt NMEA backend probes known USB serial devices and opens the first
        // match at 4800 baud. Desktop serial NMEA is owned explicitly by
        // LinkManager, so never allow the implicit position source to claim it.
        for (const QString &sourceName : availableSources) {
            if (sourceName.compare(QStringLiteral("nmea"), Qt::CaseInsensitive) == 0) {
                continue;
            }

            _defaultSource = QGeoPositionInfoSource::createSource(sourceName, this);
            if (_defaultSource) {
                qCInfo(QGCPositionManagerLog) << "Using desktop position source" << sourceName;
                break;
            }
        }
#endif

        if (!_defaultSource) {
            qCWarning(QGCPositionManagerLog) << Q_FUNC_INFO << "No default source available";
            return;
        }
    }

    _setPositionSource(QGCPositionSource::InternalGPS);
}

void QGCPositionManager::_handlePermissionStatus(Qt::PermissionStatus permissionStatus)
{
    if (permissionStatus == Qt::PermissionStatus::Granted) {
        _setupPositionSources();
    } else {
        qCWarning(QGCPositionManagerLog) << Q_FUNC_INFO << "Location Permission Denied";
    }
}

void QGCPositionManager::_checkPermission()
{
    QLocationPermission locationPermission;
    locationPermission.setAccuracy(QLocationPermission::Precise);

    const Qt::PermissionStatus permissionStatus = QCoreApplication::instance()->checkPermission(locationPermission);
    if (permissionStatus == Qt::PermissionStatus::Undetermined) {
        QCoreApplication::instance()->requestPermission(locationPermission, this, [this](const QPermission &permission) {
            _handlePermissionStatus(permission.status());
        });
    } else {
        _handlePermissionStatus(permissionStatus);
    }
}

void QGCPositionManager::setNmeaSourceDevice(QIODevice *device)
{
    (void) disconnect(_nmeaDeviceCloseConnection);
    (void) disconnect(_nmeaDeviceDestroyedConnection);

    if (_nmeaSource) {
        _nmeaSource->stopUpdates();
        (void) disconnect(_nmeaSource);

        if (_currentSource == _nmeaSource) {
            _currentSource = nullptr;
            _invalidatePosition();
        }

        delete _nmeaSource;
        _nmeaSource = nullptr;
    }

    _nmeaSourceDevice = device;

    if (!device) {
        clearNmeaRawData();
        _setNmeaSourceActive(false);
        _setPositionSource(QGCPositionManager::InternalGPS);
        return;
    }

    clearNmeaRawData();

    _nmeaDeviceCloseConnection = connect(device, &QIODevice::aboutToClose, this, [this, device]() {
        if (_nmeaSourceDevice == device) {
            _setNmeaSourceActive(false);
        }
    });
    _nmeaDeviceDestroyedConnection = connect(device, &QObject::destroyed, this, [this]() {
        _nmeaSourceDevice = nullptr;
        _setNmeaSourceActive(false);
    });

    _nmeaSource = new QNmeaPositionInfoSource(QNmeaPositionInfoSource::RealTimeMode, this);
    _nmeaSource->setDevice(device);
    _nmeaSource->setUserEquivalentRangeError(5.1);
    _setPositionSource(QGCPositionManager::NmeaGPS);
    _setNmeaSourceActive(device->isOpen());
}

void QGCPositionManager::clearNmeaRawData()
{
    if (_nmeaRawData.isEmpty()) {
        return;
    }

    _nmeaRawData.clear();
    _nmeaRawDataNotifyTimer.stop();
    emit nmeaRawDataChanged();
}

void QGCPositionManager::appendNmeaRawData(const QByteArray &data)
{
    if (data.size() >= kMaxNmeaRawDataBytes) {
        _nmeaRawData = data.right(kMaxNmeaRawDataBytes);
    } else {
        _nmeaRawData.append(data);
        const qsizetype excessBytes = _nmeaRawData.size() - kMaxNmeaRawDataBytes;
        if (excessBytes > 0) {
            _nmeaRawData.remove(0, excessBytes);
        }
    }

    if (!_nmeaRawDataNotifyTimer.isActive()) {
        _nmeaRawDataNotifyTimer.start();
    }
}

void QGCPositionManager::_setNmeaSourceActive(bool active)
{
    if (_nmeaSourceActive == active) {
        return;
    }

    _nmeaSourceActive = active;
    emit nmeaSourceActiveChanged();
}

void QGCPositionManager::_positionUpdated(const QGeoPositionInfo &update)
{
    _geoPositionInfo = update;

    const QGeoCoordinate sourceCoordinate = update.coordinate();
    _setGCSAltitude(update.isValid() && sourceCoordinate.isValid() && qIsFinite(sourceCoordinate.altitude())
        ? sourceCoordinate.altitude()
        : qQNaN());

    QGeoCoordinate newGCSPosition(_gcsPosition);

    if (update.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)) {
        if ((qAbs(update.coordinate().latitude()) > 0.001) && (qAbs(update.coordinate().longitude()) > 0.001)) {
            _gcsPositionHorizontalAccuracy = update.attribute(QGeoPositionInfo::HorizontalAccuracy);
            if (_gcsPositionHorizontalAccuracy <= kMinHorizonalAccuracyMeters) {
                newGCSPosition.setLatitude(update.coordinate().latitude());
                newGCSPosition.setLongitude(update.coordinate().longitude());
            }
            emit gcsPositionHorizontalAccuracyChanged(_gcsPositionHorizontalAccuracy);
        }
    }

    if (update.hasAttribute(QGeoPositionInfo::VerticalAccuracy)) {
        _gcsPositionVerticalAccuracy = update.attribute(QGeoPositionInfo::VerticalAccuracy);
        if (_gcsPositionVerticalAccuracy <= kMinVerticalAccuracyMeters) {
            newGCSPosition.setAltitude(update.coordinate().altitude());
        }
    }

    _gcsPositionAccuracy = sqrt(pow(_gcsPositionHorizontalAccuracy, 2) + pow(_gcsPositionVerticalAccuracy, 2));

    _setGCSPosition(newGCSPosition);

    if (update.hasAttribute(QGeoPositionInfo::DirectionAccuracy)) {
        _gcsDirectionAccuracy = update.attribute(QGeoPositionInfo::DirectionAccuracy);
        if (_gcsDirectionAccuracy <= kMinDirectionAccuracyDegrees) {
            _setGCSHeading(update.attribute(QGeoPositionInfo::Direction));
        }
    } else if (_usingPluginSource) {
        _setGCSHeading(update.attribute(QGeoPositionInfo::Direction));
    }

    emit positionInfoUpdated(update);
}

void QGCPositionManager::_setGCSHeading(qreal newGCSHeading)
{
    if (newGCSHeading != _gcsHeading) {
        _gcsHeading = newGCSHeading;
        emit gcsHeadingChanged(_gcsHeading);
    }
}

void QGCPositionManager::_setGCSPosition(const QGeoCoordinate& newGCSPosition)
{
    if (newGCSPosition != _gcsPosition) {
        _gcsPosition = newGCSPosition;
        emit gcsPositionChanged(_gcsPosition);
    }
}

void QGCPositionManager::_setGCSAltitude(qreal newGCSAltitude)
{
    if ((qIsNaN(newGCSAltitude) && qIsNaN(_gcsAltitude)) || newGCSAltitude == _gcsAltitude) {
        return;
    }

    _gcsAltitude = newGCSAltitude;
    emit gcsAltitudeChanged(_gcsAltitude);
}

void QGCPositionManager::_invalidatePosition()
{
    _geoPositionInfo = QGeoPositionInfo();
    emit positionInfoUpdated(_geoPositionInfo);

    _setGCSPosition(QGeoCoordinate());
    _setGCSAltitude(qQNaN());
    _setGCSHeading(qQNaN());

    _gcsPositionHorizontalAccuracy = std::numeric_limits<qreal>::infinity();
    _gcsPositionVerticalAccuracy = std::numeric_limits<qreal>::infinity();
    _gcsPositionAccuracy = std::numeric_limits<qreal>::infinity();
    _gcsDirectionAccuracy = std::numeric_limits<qreal>::infinity();
    emit gcsPositionHorizontalAccuracyChanged(_gcsPositionHorizontalAccuracy);
}

void QGCPositionManager::_setPositionSource(QGCPositionSource source)
{
    if (_currentSource != nullptr) {
        _currentSource->stopUpdates();
        (void) disconnect(_currentSource);
        _invalidatePosition();
    }

    switch (source) {
    case QGCPositionManager::Log:
        break;
    case QGCPositionManager::Simulated:
        _currentSource = _simulatedSource;
        break;
    case QGCPositionManager::NmeaGPS:
        _currentSource = _nmeaSource;
        break;
    case QGCPositionManager::InternalGPS:
        _currentSource = _defaultSource;
        break;
    case QGCPositionManager::ExternalGPS:
        break;
    default:
        _currentSource = _defaultSource;
        break;
    }

    if (_currentSource != nullptr) {
        _currentSource->setPreferredPositioningMethods(QGeoPositionInfoSource::SatellitePositioningMethods);
        _updateInterval = _currentSource->minimumUpdateInterval();
        #if !defined(Q_OS_DARWIN) && !defined(Q_OS_IOS)
            _currentSource->setUpdateInterval(_updateInterval);
        #endif
        (void) connect(_currentSource, &QGeoPositionInfoSource::positionUpdated, this, &QGCPositionManager::_positionUpdated);
        (void) connect(_currentSource, &QGeoPositionInfoSource::errorOccurred, this, [](QGeoPositionInfoSource::Error positioningError) {
            qCWarning(QGCPositionManagerLog) << Q_FUNC_INFO << positioningError;
        });

        // (void) connect(QGCCompass::instance(), &QGCCompass::positionUpdated, this, &QGCPositionManager::_positionUpdated);

        _currentSource->startUpdates();
    }
}
