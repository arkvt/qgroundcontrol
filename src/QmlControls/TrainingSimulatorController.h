/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtNetwork/QUdpSocket>

class TrainingSimulatorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool planeRunning READ planeRunning NOTIFY runningChanged)
    Q_PROPERTY(bool targetRunning READ targetRunning NOTIFY runningChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY runningChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString sitlExecutable READ sitlExecutable WRITE setSitlExecutable NOTIFY configurationChanged)

    Q_PROPERTY(double latitude READ latitude WRITE setLatitude NOTIFY configurationChanged)
    Q_PROPERTY(double longitude READ longitude WRITE setLongitude NOTIFY configurationChanged)
    Q_PROPERTY(double altitude READ altitude WRITE setAltitude NOTIFY configurationChanged)
    Q_PROPERTY(double heading READ heading WRITE setHeading NOTIFY configurationChanged)
    Q_PROPERTY(double targetSpeed READ targetSpeed WRITE setTargetSpeed NOTIFY configurationChanged)
    Q_PROPERTY(double targetRate READ targetRate WRITE setTargetRate NOTIFY configurationChanged)
    Q_PROPERTY(double targetRadius READ targetRadius WRITE setTargetRadius NOTIFY configurationChanged)
    Q_PROPERTY(QString targetPattern READ targetPattern WRITE setTargetPattern NOTIFY configurationChanged)
    Q_PROPERTY(int nmeaPort READ nmeaPort WRITE setNmeaPort NOTIFY configurationChanged)
    Q_PROPERTY(bool wipeEeprom READ wipeEeprom WRITE setWipeEeprom NOTIFY configurationChanged)

public:
    explicit TrainingSimulatorController(QObject *parent = nullptr);
    ~TrainingSimulatorController() override;

    bool supported() const;
    bool planeRunning() const;
    bool targetRunning() const;
    bool running() const;
    QString statusText() const;
    QString errorText() const { return _errorText; }
    QString logText() const { return _logText; }
    QString sitlExecutable() const { return _sitlExecutable; }

    double latitude() const { return _latitude; }
    double longitude() const { return _longitude; }
    double altitude() const { return _altitude; }
    double heading() const { return _heading; }
    double targetSpeed() const { return _targetSpeed; }
    double targetRate() const { return _targetRate; }
    double targetRadius() const { return _targetRadius; }
    QString targetPattern() const { return _targetPattern; }
    int nmeaPort() const { return _nmeaPort; }
    bool wipeEeprom() const { return _wipeEeprom; }

    void setSitlExecutable(const QString &value);
    void setLatitude(double value);
    void setLongitude(double value);
    void setAltitude(double value);
    void setHeading(double value);
    void setTargetSpeed(double value);
    void setTargetRate(double value);
    void setTargetRadius(double value);
    void setTargetPattern(const QString &value);
    void setNmeaPort(int value);
    void setWipeEeprom(bool value);

    Q_INVOKABLE bool startPlaneSimulation();
    Q_INVOKABLE bool startTargetSimulation();
    Q_INVOKABLE void stopPlaneSimulation();
    Q_INVOKABLE void stopTargetSimulation();
    Q_INVOKABLE void stopTraining();
    Q_INVOKABLE void clearLog();

signals:
    void runningChanged();
    void errorTextChanged();
    void logTextChanged();
    void configurationChanged();

private:
    void _loadConfiguration();
    void _saveConfigurationValue(const QString &key, const QVariant &value);
    void _setCleanupRequired(bool required);
    void _appendLog(const QString &source, const QByteArray &data);
    void _setError(const QString &error);
    void _configureQgcForTraining();
    void _restoreQgcConfiguration();
    void _stopTargetSimulation();
    void _stopPlaneProcess();
    void _handleUnexpectedExit(int exitCode);
    bool _validateLocation();
    bool _validateTargetConfiguration();
    bool _prepareEnvironment(QString &executable, QString &quadplaneDefaults, QString &followDefaults, QString &runtimeDirectory);
    QString _resolveSitlExecutable() const;
    QString _runtimeRootForExecutable(const QString &executable) const;
    void _sendTargetPosition();
    static QByteArray _nmeaSentence(const QString &body);
    static QString _nmeaCoordinate(double value, bool latitude, QChar &direction);

    QProcess _planeProcess;
    QTimer _targetTimer;
    QUdpSocket _targetSocket;
    QElapsedTimer _targetElapsed;
    bool _stopping = false;
    bool _cleanupRequired = false;
    bool _qgcConfigurationOverridden = false;
    QVariant _previousNmeaDevice;
    QVariant _previousNmeaPort;
    QVariant _previousFollowTarget;
    QVariant _previousAutoConnectUdp;
    QVariant _previousUdpListenPort;
    QString _errorText;
    QString _logText;
    QString _sitlExecutable;
    qint64 _lastTargetLogSecond = -1;
    quintptr _planeJobHandle = 0;

    double _latitude = 31.84517;
    double _longitude = 117.25010;
    double _altitude = 45.0;
    double _heading = 90.0;
    double _targetSpeed = 20.0;
    double _targetRate = 5.0;
    double _targetRadius = 30.0;
    QString _targetPattern = QStringLiteral("circle");
    int _nmeaPort = 10110;
    bool _wipeEeprom = true;

    static constexpr const char *kSettingsGroup = "TrainingSimulator";
};
