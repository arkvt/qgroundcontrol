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
#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

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

    Q_PROPERTY(QString wslDistribution READ wslDistribution WRITE setWslDistribution NOTIFY configurationChanged)
    Q_PROPERTY(QString ardupilotPath READ ardupilotPath WRITE setArdupilotPath NOTIFY configurationChanged)
    Q_PROPERTY(QString pythonExecutable READ pythonExecutable WRITE setPythonExecutable NOTIFY configurationChanged)
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

    QString wslDistribution() const { return _wslDistribution; }
    QString ardupilotPath() const { return _ardupilotPath; }
    QString pythonExecutable() const { return _pythonExecutable; }
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

    void setWslDistribution(const QString &value);
    void setArdupilotPath(const QString &value);
    void setPythonExecutable(const QString &value);
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

    Q_INVOKABLE bool startTraining();
    Q_INVOKABLE bool checkEnvironment();
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
    void _setTargetProcessId(qint64 processId);
    void _appendLog(const QString &source, const QByteArray &data);
    void _setError(const QString &error);
    void _configureQgcForTraining();
    void _restoreQgcConfiguration();
    void _stopTargetProcess();
    void _stopPlaneProcess();
    void _handleUnexpectedExit(const QString &processName, int exitCode);
    bool _prepareEnvironment(QString &wsl, QString &python, QString &scriptPath);
    bool _checkWslEnvironment(const QString &wsl, QString *windowsHostAddress);
    bool _checkWslUdpReachability(const QString &wsl, const QString &windowsHostAddress);
    bool _selectAvailableWslDistribution(const QString &wsl);
    QStringList _installedWslDistributions(const QString &wsl) const;
    QString _extractTargetScript();
    QString _resolvePythonExecutable(QString *versionText = nullptr) const;
    QString _resolveExecutable(const QString &configured, const QStringList &fallbacks) const;
    QString _buildEnvironmentProbeCommand() const;
    QString _buildPlaneCommand() const;
    static QString _shellQuote(QString value);

    QProcess _planeProcess;
    QProcess _targetProcess;
    bool _stopping = false;
    bool _cleanupRequired = false;
    qint64 _targetProcessId = 0;
    bool _qgcConfigurationOverridden = false;
    QVariant _previousNmeaDevice;
    QVariant _previousNmeaPort;
    QVariant _previousFollowTarget;
    QVariant _previousAutoConnectUdp;
    QVariant _previousUdpListenPort;
    QString _errorText;
    QString _logText;

    QString _wslDistribution = QStringLiteral("ubuntu_22.04");
    QString _ardupilotPath = QStringLiteral("/home/ubuntu/ardupilot");
    QString _pythonExecutable = QStringLiteral("python");
    double _latitude = 31.8511168;
    double _longitude = 117.2292701;
    double _altitude = 50.0;
    double _heading = 0.0;
    double _targetSpeed = 20.0;
    double _targetRate = 5.0;
    double _targetRadius = 30.0;
    QString _targetPattern = QStringLiteral("circle");
    int _nmeaPort = 10110;
    bool _wipeEeprom = true;

    static constexpr const char *kSettingsGroup = "TrainingSimulator";
    static constexpr const char *kPidFile = "/tmp/qgc_training_simulator.pid";
};
