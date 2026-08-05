/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TrainingSimulatorController.h"

#include "AppSettings.h"
#include "AutoConnectSettings.h"
#include "Fact.h"
#include "SettingsManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringDecoder>
#include <QtCore/QTimer>
#include <QtNetwork/QUdpSocket>

TrainingSimulatorController::TrainingSimulatorController(QObject *parent)
    : QObject(parent)
{
    _loadConfiguration();

    _planeProcess.setProcessChannelMode(QProcess::MergedChannels);
    _targetProcess.setProcessChannelMode(QProcess::MergedChannels);

    connect(&_planeProcess, &QProcess::readyRead, this, [this]() {
        _appendLog(tr("飞机"), _planeProcess.readAll());
    });
    connect(&_targetProcess, &QProcess::readyRead, this, [this]() {
        _appendLog(tr("小车"), _targetProcess.readAll());
    });
    connect(&_planeProcess, &QProcess::stateChanged, this, &TrainingSimulatorController::runningChanged);
    connect(&_targetProcess, &QProcess::stateChanged, this, &TrainingSimulatorController::runningChanged);

    connect(&_planeProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!_stopping && (error == QProcess::FailedToStart || error == QProcess::Crashed)) {
            _setError(tr("飞机仿真进程错误：%1").arg(_planeProcess.errorString()));
        }
    });
    connect(&_targetProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!_stopping && (error == QProcess::FailedToStart || error == QProcess::Crashed)) {
            _setError(tr("小车仿真进程错误：%1").arg(_targetProcess.errorString()));
        }
    });

    connect(&_planeProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        _handleUnexpectedExit(tr("飞机"), exitCode);
    });
    connect(&_targetProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        _handleUnexpectedExit(tr("小车"), exitCode);
    });

    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, &TrainingSimulatorController::stopTraining, Qt::DirectConnection);
    }

    // A forced termination cannot emit aboutToQuit. On the next launch, use the
    // persisted marker to remove any SITL process left by the previous session.
    if (_cleanupRequired) {
        QTimer::singleShot(0, this, [this]() {
            _stopping = true;
            _stopTargetProcess();
            _stopPlaneProcess();
            _restoreQgcConfiguration();
            _setCleanupRequired(false);
            _stopping = false;
            _appendLog(tr("系统"), tr("已清理上次异常退出遗留的仿真进程。\n").toUtf8());
            emit runningChanged();
        });
    }
}

TrainingSimulatorController::~TrainingSimulatorController()
{
    stopTraining();
}

bool TrainingSimulatorController::supported() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool TrainingSimulatorController::planeRunning() const
{
    return _planeProcess.state() != QProcess::NotRunning;
}

bool TrainingSimulatorController::targetRunning() const
{
    return _targetProcess.state() != QProcess::NotRunning;
}

bool TrainingSimulatorController::running() const
{
    return planeRunning() || targetRunning();
}

QString TrainingSimulatorController::statusText() const
{
    if (planeRunning() && targetRunning()) {
        return tr("飞机与小车仿真正在运行");
    }
    if (planeRunning()) {
        return tr("飞机仿真正在运行，等待小车脚本");
    }
    if (targetRunning()) {
        return tr("小车脚本正在运行，等待飞机 SITL");
    }
    return tr("仿真未启动");
}

void TrainingSimulatorController::_loadConfiguration()
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    _wslDistribution = settings.value(QStringLiteral("wslDistribution"), _wslDistribution).toString();
    _ardupilotPath = settings.value(QStringLiteral("ardupilotPath"), _ardupilotPath).toString();
    _pythonExecutable = settings.value(QStringLiteral("pythonExecutable"), _pythonExecutable).toString();
    _latitude = settings.value(QStringLiteral("latitude"), _latitude).toDouble();
    _longitude = settings.value(QStringLiteral("longitude"), _longitude).toDouble();
    _altitude = settings.value(QStringLiteral("altitude"), _altitude).toDouble();
    _heading = settings.value(QStringLiteral("heading"), _heading).toDouble();
    _targetSpeed = settings.value(QStringLiteral("targetSpeed"), _targetSpeed).toDouble();
    _targetRate = settings.value(QStringLiteral("targetRate"), _targetRate).toDouble();
    _targetRadius = settings.value(QStringLiteral("targetRadius"), _targetRadius).toDouble();
    _targetPattern = settings.value(QStringLiteral("targetPattern"), _targetPattern).toString();
    _nmeaPort = settings.value(QStringLiteral("nmeaPort"), _nmeaPort).toInt();
    _wipeEeprom = settings.value(QStringLiteral("wipeEeprom"), _wipeEeprom).toBool();
    _cleanupRequired = settings.value(QStringLiteral("cleanupRequired"), false).toBool();
    _targetProcessId = settings.value(QStringLiteral("targetProcessId"), 0).toLongLong();
    _qgcConfigurationOverridden = settings.value(QStringLiteral("qgcConfigurationOverridden"), false).toBool();
    _previousNmeaDevice = settings.value(QStringLiteral("previousNmeaDevice"));
    _previousNmeaPort = settings.value(QStringLiteral("previousNmeaPort"));
    _previousFollowTarget = settings.value(QStringLiteral("previousFollowTarget"));
    _previousAutoConnectUdp = settings.value(QStringLiteral("previousAutoConnectUdp"));
    _previousUdpListenPort = settings.value(QStringLiteral("previousUdpListenPort"));
}

void TrainingSimulatorController::_saveConfigurationValue(const QString &key, const QVariant &value)
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(key, value);
}

void TrainingSimulatorController::_setCleanupRequired(bool required)
{
    if (_cleanupRequired == required) {
        return;
    }
    _cleanupRequired = required;
    _saveConfigurationValue(QStringLiteral("cleanupRequired"), required);
}

void TrainingSimulatorController::_setTargetProcessId(qint64 processId)
{
    _targetProcessId = processId;
    _saveConfigurationValue(QStringLiteral("targetProcessId"), processId);
}

#define DEFINE_STRING_SETTER(Name, Member, Key) \
    void TrainingSimulatorController::set##Name(const QString &value) \
    { \
        const QString trimmed = value.trimmed(); \
        if (Member == trimmed) { return; } \
        Member = trimmed; \
        _saveConfigurationValue(QStringLiteral(Key), Member); \
        emit configurationChanged(); \
    }

#define DEFINE_DOUBLE_SETTER(Name, Member, Key) \
    void TrainingSimulatorController::set##Name(double value) \
    { \
        if (Member == value) { return; } \
        Member = value; \
        _saveConfigurationValue(QStringLiteral(Key), Member); \
        emit configurationChanged(); \
    }

DEFINE_STRING_SETTER(WslDistribution, _wslDistribution, "wslDistribution")
DEFINE_STRING_SETTER(ArdupilotPath, _ardupilotPath, "ardupilotPath")
DEFINE_STRING_SETTER(PythonExecutable, _pythonExecutable, "pythonExecutable")
DEFINE_DOUBLE_SETTER(Latitude, _latitude, "latitude")
DEFINE_DOUBLE_SETTER(Longitude, _longitude, "longitude")
DEFINE_DOUBLE_SETTER(Altitude, _altitude, "altitude")
DEFINE_DOUBLE_SETTER(Heading, _heading, "heading")
DEFINE_DOUBLE_SETTER(TargetSpeed, _targetSpeed, "targetSpeed")
DEFINE_DOUBLE_SETTER(TargetRate, _targetRate, "targetRate")
DEFINE_DOUBLE_SETTER(TargetRadius, _targetRadius, "targetRadius")
DEFINE_STRING_SETTER(TargetPattern, _targetPattern, "targetPattern")

#undef DEFINE_STRING_SETTER
#undef DEFINE_DOUBLE_SETTER

void TrainingSimulatorController::setNmeaPort(int value)
{
    if (_nmeaPort == value) {
        return;
    }
    _nmeaPort = value;
    _saveConfigurationValue(QStringLiteral("nmeaPort"), _nmeaPort);
    emit configurationChanged();
}

void TrainingSimulatorController::setWipeEeprom(bool value)
{
    if (_wipeEeprom == value) {
        return;
    }
    _wipeEeprom = value;
    _saveConfigurationValue(QStringLiteral("wipeEeprom"), _wipeEeprom);
    emit configurationChanged();
}

bool TrainingSimulatorController::startTraining()
{
#ifndef Q_OS_WIN
    _setError(tr("该训练功能当前仅支持 Windows + WSL 环境。"));
    return false;
#else
    if (running()) {
        _setError(tr("仿真已经启动，请先停止后再重新启动。"));
        return false;
    }
    if (_wslDistribution.isEmpty() || _ardupilotPath.isEmpty()) {
        _setError(tr("请填写 WSL 发行版名称和 ArduPilot 源码路径。"));
        return false;
    }
    if (_latitude < -90.0 || _latitude > 90.0 || _longitude < -180.0 || _longitude > 180.0) {
        _setError(tr("起始点经纬度超出有效范围。"));
        return false;
    }
    if (_altitude < -1000.0 || _altitude > 100000.0 || _heading < 0.0 || _heading >= 360.0) {
        _setError(tr("高度或航向超出有效范围；航向应为 0（含）到 360（不含）度。"));
        return false;
    }
    if (_targetSpeed < 0.0 || _targetRate <= 0.0 || _targetRadius < 1.0) {
        _setError(tr("小车速度、输出频率或转弯半径设置无效。"));
        return false;
    }
    if (_nmeaPort < 1 || _nmeaPort > 65535) {
        _setError(tr("NMEA UDP 端口必须在 1 到 65535 之间。"));
        return false;
    }
    const QStringList patterns{QStringLiteral("circle"), QStringLiteral("east"), QStringLiteral("north"), QStringLiteral("line")};
    if (!patterns.contains(_targetPattern)) {
        _setError(tr("小车运动轨迹设置无效。"));
        return false;
    }

    _setError(QString());
    clearLog();
    QString wsl;
    QString python;
    QString scriptPath;
    if (!_prepareEnvironment(wsl, python, scriptPath)) {
        return false;
    }

    _configureQgcForTraining();
    _appendLog(tr("系统"), tr("已启用 NMEA UDP %1、MAVLink UDP 14550，并将跟随目标发送策略临时设为“始终”。\n")
                                  .arg(_nmeaPort).toUtf8());

    QStringList targetArguments;
    if (QFileInfo(python).completeBaseName().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0) {
        targetArguments << QStringLiteral("-3");
    }
    targetArguments << QStringLiteral("-u") << QDir::toNativeSeparators(scriptPath)
                    << QStringLiteral("--host") << QStringLiteral("127.0.0.1")
                    << QStringLiteral("--port") << QString::number(_nmeaPort)
                    << QStringLiteral("--lat") << QString::number(_latitude, 'f', 8)
                    << QStringLiteral("--lon") << QString::number(_longitude, 'f', 8)
                    << QStringLiteral("--alt") << QString::number(_altitude, 'f', 2)
                    << QStringLiteral("--speed") << QString::number(_targetSpeed, 'f', 2)
                    << QStringLiteral("--rate") << QString::number(_targetRate, 'f', 2)
                    << QStringLiteral("--pattern") << _targetPattern
                    << QStringLiteral("--radius") << QString::number(_targetRadius, 'f', 2);
    _targetProcess.setProgram(python);
    _targetProcess.setArguments(targetArguments);
    _targetProcess.setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    _targetProcess.start();
    if (!_targetProcess.waitForStarted(3000)) {
        _setError(tr("小车脚本启动失败：%1").arg(_targetProcess.errorString()));
        _restoreQgcConfiguration();
        return false;
    }
    _setTargetProcessId(_targetProcess.processId());
    _setCleanupRequired(true);

    _planeProcess.setProgram(wsl);
    _planeProcess.setArguments({QStringLiteral("-d"), _wslDistribution, QStringLiteral("--exec"),
                                QStringLiteral("setsid"), QStringLiteral("--wait"), QStringLiteral("bash"), QStringLiteral("-lc"),
                                _buildPlaneCommand()});
    // QGC is often run from a substituted deployment drive (for example Q:). WSL cannot
    // translate such a working directory, so always launch it from a real local directory.
    _planeProcess.setWorkingDirectory(QDir::tempPath());
    _planeProcess.start();
    if (!_planeProcess.waitForStarted(5000)) {
        _setError(tr("飞机 SITL 启动失败：%1").arg(_planeProcess.errorString()));
        stopTraining();
        return false;
    }

    _appendLog(tr("系统"), tr("启动命令已发出。飞机连接通常需要数十秒，请观察状态栏中的载具连接。\n").toUtf8());
    emit runningChanged();
    return true;
#endif
}

bool TrainingSimulatorController::checkEnvironment()
{
#ifndef Q_OS_WIN
    _setError(tr("该训练功能当前仅支持 Windows + WSL 环境。"));
    return false;
#else
    if (running()) {
        _setError(tr("请先停止仿真，再检查运行环境。"));
        return false;
    }
    _setError(QString());
    clearLog();
    QString wsl;
    QString python;
    QString scriptPath;
    return _prepareEnvironment(wsl, python, scriptPath);
#endif
}

void TrainingSimulatorController::stopTraining()
{
    if (_stopping) {
        return;
    }
    _stopping = true;
    _stopTargetProcess();
    _stopPlaneProcess();
    _restoreQgcConfiguration();
    _setCleanupRequired(false);
    _stopping = false;
    emit runningChanged();
}

void TrainingSimulatorController::clearLog()
{
    if (_logText.isEmpty()) {
        return;
    }
    _logText.clear();
    emit logTextChanged();
}

void TrainingSimulatorController::_appendLog(const QString &source, const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }
    QString text = QString::fromUtf8(data);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QString prefix = QStringLiteral("[%1] ").arg(source);
    text.replace(QStringLiteral("\n"), QStringLiteral("\n") + prefix);
    _logText += prefix + text;
    if (_logText.endsWith(prefix)) {
        _logText.chop(prefix.size());
    }
    constexpr int maxLogCharacters = 60000;
    if (_logText.size() > maxLogCharacters) {
        _logText = _logText.right(maxLogCharacters);
    }
    emit logTextChanged();
}

void TrainingSimulatorController::_setError(const QString &error)
{
    if (_errorText == error) {
        return;
    }
    _errorText = error;
    emit errorTextChanged();
}

void TrainingSimulatorController::_configureQgcForTraining()
{
    if (_qgcConfigurationOverridden) {
        return;
    }
    AutoConnectSettings *const autoConnect = SettingsManager::instance()->autoConnectSettings();
    AppSettings *const app = SettingsManager::instance()->appSettings();
    _previousNmeaDevice = autoConnect->autoConnectNmeaPort()->rawValue();
    _previousNmeaPort = autoConnect->nmeaUdpPort()->rawValue();
    _previousFollowTarget = app->followTarget()->rawValue();
    _previousAutoConnectUdp = autoConnect->autoConnectUDP()->rawValue();
    _previousUdpListenPort = autoConnect->udpListenPort()->rawValue();
    _qgcConfigurationOverridden = true;

    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(QStringLiteral("previousNmeaDevice"), _previousNmeaDevice);
    settings.setValue(QStringLiteral("previousNmeaPort"), _previousNmeaPort);
    settings.setValue(QStringLiteral("previousFollowTarget"), _previousFollowTarget);
    settings.setValue(QStringLiteral("previousAutoConnectUdp"), _previousAutoConnectUdp);
    settings.setValue(QStringLiteral("previousUdpListenPort"), _previousUdpListenPort);
    settings.setValue(QStringLiteral("qgcConfigurationOverridden"), true);

    autoConnect->nmeaUdpPort()->setRawValue(_nmeaPort);
    autoConnect->autoConnectNmeaPort()->setRawValue(QStringLiteral("UDP Port"));
    autoConnect->udpListenPort()->setRawValue(14550);
    autoConnect->autoConnectUDP()->setRawValue(true);
    app->followTarget()->setRawValue(1); // Always send converted NMEA position as FOLLOW_TARGET.
}

void TrainingSimulatorController::_restoreQgcConfiguration()
{
    if (!_qgcConfigurationOverridden) {
        return;
    }
    AutoConnectSettings *const autoConnect = SettingsManager::instance()->autoConnectSettings();
    AppSettings *const app = SettingsManager::instance()->appSettings();
    if (_previousUdpListenPort.isValid()) {
        autoConnect->udpListenPort()->setRawValue(_previousUdpListenPort);
    }
    if (_previousAutoConnectUdp.isValid()) {
        autoConnect->autoConnectUDP()->setRawValue(_previousAutoConnectUdp);
    }
    if (_previousNmeaDevice.isValid()) {
        autoConnect->autoConnectNmeaPort()->setRawValue(_previousNmeaDevice);
    }
    if (_previousNmeaPort.isValid()) {
        autoConnect->nmeaUdpPort()->setRawValue(_previousNmeaPort);
    }
    if (_previousFollowTarget.isValid()) {
        app->followTarget()->setRawValue(_previousFollowTarget);
    }
    _qgcConfigurationOverridden = false;
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(QStringLiteral("qgcConfigurationOverridden"), false);
    settings.remove(QStringLiteral("previousNmeaDevice"));
    settings.remove(QStringLiteral("previousNmeaPort"));
    settings.remove(QStringLiteral("previousFollowTarget"));
    settings.remove(QStringLiteral("previousAutoConnectUdp"));
    settings.remove(QStringLiteral("previousUdpListenPort"));
    _appendLog(tr("系统"), tr("已恢复启动仿真前的 QGC NMEA 与跟随目标设置。\n").toUtf8());
}

void TrainingSimulatorController::_stopTargetProcess()
{
    if (_targetProcess.state() != QProcess::NotRunning) {
        _targetProcess.terminate();
        if (!_targetProcess.waitForFinished(1500)) {
            _targetProcess.kill();
            _targetProcess.waitForFinished(1000);
        }
    }

#ifdef Q_OS_WIN
    if (_targetProcessId > 0) {
        const QString powershell = _resolveExecutable(QStringLiteral("powershell.exe"),
                                                       {QStringLiteral("powershell.exe"), QStringLiteral("powershell")});
        if (!powershell.isEmpty()) {
            const QString command = QStringLiteral(
                "$p = Get-CimInstance Win32_Process -Filter 'ProcessId = %1' -ErrorAction SilentlyContinue; "
                "if ($p -and $p.CommandLine -like '*simulate_rtk_nmea_udp.py*') { "
                "Stop-Process -Id %1 -Force -ErrorAction SilentlyContinue }")
                                        .arg(_targetProcessId);
            QProcess cleanup;
            cleanup.setWorkingDirectory(QDir::tempPath());
            cleanup.start(powershell, {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                                       QStringLiteral("-Command"), command});
            cleanup.waitForFinished(3000);
        }
    }
#endif
    _setTargetProcessId(0);
}

void TrainingSimulatorController::_stopPlaneProcess()
{
#ifdef Q_OS_WIN
    if (_planeProcess.state() != QProcess::NotRunning || _cleanupRequired) {
        const QString wsl = _resolveExecutable(QStringLiteral("wsl.exe"), {QStringLiteral("wsl.exe"), QStringLiteral("wsl")});
        if (!wsl.isEmpty()) {
            const QString cleanupCommand = QStringLiteral(
                "if read -r sim_pid < %1 2>/dev/null; then "
                "kill -TERM -- -\"$sim_pid\" 2>/dev/null || true; "
                "for i in 1 2 3 4 5; do kill -0 -- -\"$sim_pid\" 2>/dev/null || break; sleep 0.2; done; "
                "kill -KILL -- -\"$sim_pid\" 2>/dev/null || true; fi; "
                "if command -v pgrep >/dev/null 2>&1; then "
                "for stale_pid in $(pgrep -f '^build/sitl/bin/arduplane .*qgc_training_follow.params' || true); do "
                "kill -TERM \"$stale_pid\" 2>/dev/null || true; done; sleep 0.2; "
                "for stale_pid in $(pgrep -f '^build/sitl/bin/arduplane .*qgc_training_follow.params' || true); do "
                "kill -KILL \"$stale_pid\" 2>/dev/null || true; done; "
                "for stale_pid in $(pgrep -f '^setsid --wait bash -lc .*qgc_training_follow.params' || true); do "
                "kill -TERM \"$stale_pid\" 2>/dev/null || true; done; fi; "
                "rm -f %1 /tmp/qgc_training_follow.params")
                                               .arg(_shellQuote(QString::fromLatin1(kPidFile)));
            QProcess cleanup;
            cleanup.setWorkingDirectory(QDir::tempPath());
            cleanup.start(wsl, {QStringLiteral("-d"), _wslDistribution, QStringLiteral("--exec"),
                                QStringLiteral("bash"), QStringLiteral("-lc"), cleanupCommand});
            cleanup.waitForFinished(3000);
        }
    }
#endif
    if (_planeProcess.state() == QProcess::NotRunning) {
        return;
    }
    _planeProcess.terminate();
    if (!_planeProcess.waitForFinished(1500)) {
        _planeProcess.kill();
        _planeProcess.waitForFinished(1000);
    }
}

void TrainingSimulatorController::_handleUnexpectedExit(const QString &processName, int exitCode)
{
    emit runningChanged();
    if (_stopping) {
        return;
    }
    _setError(tr("%1仿真意外退出（退出码 %2），已停止本次训练。请查看运行日志。")
                  .arg(processName).arg(exitCode));
    QTimer::singleShot(0, this, &TrainingSimulatorController::stopTraining);
}

bool TrainingSimulatorController::_prepareEnvironment(QString &wsl, QString &python, QString &scriptPath)
{
    if (_wslDistribution.isEmpty() || _ardupilotPath.isEmpty()) {
        _setError(tr("请填写 WSL 发行版名称和 ArduPilot 源码路径。"));
        return false;
    }

    wsl = _resolveExecutable(QStringLiteral("wsl.exe"), {QStringLiteral("wsl.exe"), QStringLiteral("wsl")});
    if (wsl.isEmpty()) {
        _setError(tr("未找到 wsl.exe，请先安装或更新 WSL。"));
        return false;
    }
    if (!_selectAvailableWslDistribution(wsl)) {
        return false;
    }

    QString pythonVersion;
    python = _resolvePythonExecutable(&pythonVersion);
    if (python.isEmpty()) {
        _setError(tr("未找到可用的 Python 3.9 或更高版本。请安装 Python，或填写 python.exe/py.exe 的完整路径。"));
        return false;
    }
    _appendLog(tr("环境"), tr("Windows Python %1：%2\n").arg(pythonVersion, python).toUtf8());

    scriptPath = _extractTargetScript();
    QString windowsHostAddress;
    if (scriptPath.isEmpty() || !_checkWslEnvironment(wsl, &windowsHostAddress)
        || !_checkWslUdpReachability(wsl, windowsHostAddress)) {
        return false;
    }

    _appendLog(tr("环境"), tr("检查通过，可以启动飞机与小车仿真。\n").toUtf8());
    return true;
}

bool TrainingSimulatorController::_selectAvailableWslDistribution(const QString &wsl)
{
    const QStringList installed = _installedWslDistributions(wsl);
    if (installed.isEmpty()) {
        _setError(tr("没有检测到已安装的 WSL 发行版。请先安装 Ubuntu/WSL2。"));
        return false;
    }

    QString configuredMatch;
    for (const QString &distribution : installed) {
        if (distribution.compare(_wslDistribution, Qt::CaseInsensitive) == 0) {
            configuredMatch = distribution;
            break;
        }
    }

    QStringList candidates;
    if (!configuredMatch.isEmpty()) {
        candidates.append(configuredMatch);
    }
    QStringList otherCandidates;
    for (const QString &distribution : installed) {
        if (distribution == configuredMatch) {
            continue;
        }
        if (distribution.contains(QStringLiteral("ubuntu"), Qt::CaseInsensitive)) {
            candidates.append(distribution);
        } else if (!distribution.contains(QStringLiteral("docker"), Qt::CaseInsensitive)
                   && !distribution.contains(QStringLiteral("window-agent"), Qt::CaseInsensitive)) {
            otherCandidates.append(distribution);
        }
    }
    candidates.append(otherCandidates);

    const QString simVehiclePath = QDir::cleanPath(_ardupilotPath + QStringLiteral("/Tools/autotest/sim_vehicle.py"));
    for (const QString &distribution : candidates) {
        QProcess probe;
        probe.setWorkingDirectory(QDir::tempPath());
        probe.start(wsl, {QStringLiteral("-d"), distribution, QStringLiteral("--exec"),
                          QStringLiteral("test"), QStringLiteral("-x"), simVehiclePath});
        if (probe.waitForStarted(3000) && probe.waitForFinished(7000)
            && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            if (distribution == _wslDistribution) {
                return true;
            }
            const QString previous = _wslDistribution;
            setWslDistribution(distribution);
            _appendLog(tr("环境"), tr("配置的 WSL 发行版“%1”不可用，已自动选择“%2”。\n")
                                      .arg(previous, distribution).toUtf8());
            return true;
        }
        if (probe.state() != QProcess::NotRunning) {
            probe.kill();
            probe.waitForFinished(500);
        }
    }

    _setError(tr("找不到同时包含 %1 的可用 WSL 发行版。已安装：%2")
                  .arg(simVehiclePath, installed.join(QStringLiteral(", "))));
    return false;
}

QStringList TrainingSimulatorController::_installedWslDistributions(const QString &wsl) const
{
    QProcess process;
    process.setWorkingDirectory(QDir::tempPath());
    process.start(wsl, {QStringLiteral("--list"), QStringLiteral("--quiet")});
    if (!process.waitForStarted(3000) || !process.waitForFinished(5000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(500);
        }
        return {};
    }

    const QByteArray bytes = process.readAllStandardOutput();
    QString output;
    if (bytes.contains('\0')) {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        output = decoder.decode(bytes);
    } else {
        output = QString::fromLocal8Bit(bytes);
    }

    output.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList distributions;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString name = line.trimmed();
        if (!name.isEmpty()) {
            distributions.append(name);
        }
    }
    return distributions;
}

bool TrainingSimulatorController::_checkWslEnvironment(const QString &wsl, QString *windowsHostAddress)
{
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.setWorkingDirectory(QDir::tempPath());
    probe.start(wsl, {QStringLiteral("-d"), _wslDistribution, QStringLiteral("--exec"),
                      QStringLiteral("bash"), QStringLiteral("-lc"), _buildEnvironmentProbeCommand()});
    if (!probe.waitForStarted(5000)) {
        _setError(tr("无法启动 WSL 发行版“%1”：%2").arg(_wslDistribution, probe.errorString()));
        return false;
    }
    if (!probe.waitForFinished(15000)) {
        probe.kill();
        probe.waitForFinished(1000);
        _setError(tr("WSL 环境检查超时。请先在 PowerShell 中运行 wsl -d %1，确认该发行版可以正常启动。")
                      .arg(_wslDistribution));
        return false;
    }

    const QByteArray output = probe.readAll();
    _appendLog(tr("环境"), output);
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        QString detail = QString::fromUtf8(output).trimmed();
        if (detail.size() > 800) {
            detail = detail.right(800);
        }
        _setError(tr("WSL/SITL 环境检查失败（退出码 %1）：%2")
                      .arg(probe.exitCode()).arg(detail));
        return false;
    }

    const QString outputText = QString::fromUtf8(output);
    const QString hostMarker = QStringLiteral("QGC_WINDOWS_HOST=");
    for (const QString &line : outputText.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.startsWith(hostMarker)) {
            *windowsHostAddress = line.mid(hostMarker.size()).trimmed();
            break;
        }
    }
    if (windowsHostAddress->isEmpty()) {
        _setError(tr("WSL 环境检查没有返回 Windows 主机地址。"));
        return false;
    }
    return true;
}

bool TrainingSimulatorController::_checkWslUdpReachability(const QString &wsl, const QString &windowsHostAddress)
{
    QUdpSocket receiver;
    if (!receiver.bind(QHostAddress::AnyIPv4, 0)) {
        _setError(tr("无法创建 WSL 网络自检端口：%1").arg(receiver.errorString()));
        return false;
    }

    const QByteArray expected("QGC_SIM_PROBE");
    const QString sendCommand = QStringLiteral("printf QGC_SIM_PROBE > /dev/udp/%1/%2")
                                    .arg(windowsHostAddress).arg(receiver.localPort());
    QProcess sender;
    sender.setProcessChannelMode(QProcess::MergedChannels);
    sender.setWorkingDirectory(QDir::tempPath());
    sender.start(wsl, {QStringLiteral("-d"), _wslDistribution, QStringLiteral("--exec"),
                       QStringLiteral("bash"), QStringLiteral("-lc"), sendCommand});
    if (!sender.waitForStarted(3000) || !sender.waitForFinished(5000)
        || sender.exitStatus() != QProcess::NormalExit || sender.exitCode() != 0) {
        if (sender.state() != QProcess::NotRunning) {
            sender.kill();
            sender.waitForFinished(500);
        }
        _setError(tr("WSL 无法向 Windows 发送 UDP 自检数据：%1")
                      .arg(QString::fromUtf8(sender.readAll()).trimmed()));
        return false;
    }
    if (!receiver.hasPendingDatagrams() && !receiver.waitForReadyRead(3000)) {
        _setError(tr("Windows 未收到来自 WSL 的 UDP 自检数据。请检查 Windows 防火墙是否允许 QGroundControl 接收专用网络数据。"));
        return false;
    }

    QByteArray datagram;
    datagram.resize(static_cast<qsizetype>(receiver.pendingDatagramSize()));
    if (receiver.readDatagram(datagram.data(), datagram.size()) != datagram.size() || datagram != expected) {
        _setError(tr("收到的 WSL UDP 自检数据不完整。"));
        return false;
    }
    _appendLog(tr("环境"), tr("WSL → Windows UDP 通信正常。\n").toUtf8());
    return true;
}

QString TrainingSimulatorController::_extractTargetScript()
{
    QFile source(QStringLiteral(":/training/simulate_rtk_nmea_udp.py"));
    if (!source.open(QIODevice::ReadOnly)) {
        _setError(tr("无法读取内置小车仿真脚本。"));
        return QString();
    }
    const QByteArray contents = source.readAll();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                              + QStringLiteral("/training");
    if (!QDir().mkpath(directory)) {
        _setError(tr("无法创建仿真脚本缓存目录：%1").arg(directory));
        return QString();
    }
    const QString path = directory + QStringLiteral("/simulate_rtk_nmea_udp.py");
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == contents) {
        return path;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() || !output.commit()) {
        _setError(tr("无法释放小车仿真脚本到：%1").arg(path));
        return QString();
    }
    return path;
}

QString TrainingSimulatorController::_resolvePythonExecutable(QString *versionText) const
{
    QStringList candidates;
    if (!_pythonExecutable.trimmed().isEmpty()) {
        candidates << _pythonExecutable.trimmed();
    }
    candidates << QStringLiteral("py.exe") << QStringLiteral("py")
               << QStringLiteral("python3.exe") << QStringLiteral("python3")
               << QStringLiteral("python.exe") << QStringLiteral("python");

    QStringList checkedPaths;
    for (const QString &candidate : candidates) {
        const QString executable = _resolveExecutable(candidate, QStringList{});
        if (executable.isEmpty() || checkedPaths.contains(executable, Qt::CaseInsensitive)) {
            continue;
        }
        checkedPaths << executable;

        QStringList arguments;
        if (QFileInfo(executable).completeBaseName().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0) {
            arguments << QStringLiteral("-3");
        }
        arguments << QStringLiteral("-c")
                  << QStringLiteral("import sys; print('.'.join(map(str, sys.version_info[:3]))); "
                                    "raise SystemExit(0 if sys.version_info >= (3, 9) else 9)");

        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.setWorkingDirectory(QDir::tempPath());
        probe.start(executable, arguments);
        if (!probe.waitForStarted(3000) || !probe.waitForFinished(5000)) {
            probe.kill();
            probe.waitForFinished(500);
            continue;
        }
        const QString version = QString::fromLocal8Bit(probe.readAll()).trimmed();
        if (probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            if (versionText) {
                *versionText = version;
            }
            return executable;
        }
    }
    return QString();
}

QString TrainingSimulatorController::_resolveExecutable(const QString &configured, const QStringList &fallbacks) const
{
    const QString trimmed = configured.trimmed();
    if (!trimmed.isEmpty()) {
        const QFileInfo info(trimmed);
        if (info.isAbsolute() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
        const QString found = QStandardPaths::findExecutable(trimmed);
        if (!found.isEmpty()) {
            return found;
        }
    }
    for (const QString &fallback : fallbacks) {
        const QString found = QStandardPaths::findExecutable(fallback);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return QString();
}

QString TrainingSimulatorController::_buildEnvironmentProbeCommand() const
{
    return QStringLiteral(
               "set -e\n"
               "ardupilot_dir=%1\n"
               "if [ ! -d \"$ardupilot_dir\" ]; then echo \"ERROR: ArduPilot path not found: $ardupilot_dir\"; exit 20; fi\n"
               "if [ ! -x \"$ardupilot_dir/Tools/autotest/sim_vehicle.py\" ]; then "
               "echo \"ERROR: sim_vehicle.py is missing or not executable\"; exit 21; fi\n"
               "for tool in bash python3 g++ setsid pgrep sed awk head; do if ! command -v \"$tool\" >/dev/null 2>&1; then "
               "echo \"ERROR: required WSL command is missing: $tool\"; exit 22; fi; done\n"
               "if ! (cd \"$ardupilot_dir\" && Tools/autotest/sim_vehicle.py --help >/dev/null 2>&1); then "
               "echo \"ERROR: sim_vehicle.py dependency check failed\"; exit 24; fi\n"
               "networking_mode=unknown\n"
               "if command -v wslinfo >/dev/null 2>&1; then "
               "networking_mode=\"$(wslinfo --networking-mode 2>/dev/null || echo unknown)\"; fi\n"
               "if [ \"$networking_mode\" = mirrored ]; then windows_host=127.0.0.1; else "
               "if ! command -v ip >/dev/null 2>&1; then echo \"ERROR: required WSL command is missing: ip\"; exit 22; fi; "
               "windows_host=\"$(ip -4 route show default 2>/dev/null | sed -n 's/^default via \\([^ ]*\\).*/\\1/p' | head -n 1)\"; "
               "if [ -z \"$windows_host\" ]; then windows_host=\"$(awk '/^nameserver[[:space:]]+/ { print $2; exit }' "
               "/etc/resolv.conf 2>/dev/null)\"; fi; fi\n"
               "if [ -z \"$windows_host\" ]; then echo \"ERROR: cannot determine the Windows host address\"; exit 23; fi\n"
               "case \"$windows_host\" in *[!0-9.]*|'') echo \"ERROR: invalid Windows host address: $windows_host\"; exit 23;; esac\n"
               "printf 'WSL kernel: %s\\n' \"$(uname -r)\"\n"
               "printf 'WSL networking mode: %s\\n' \"$networking_mode\"\n"
               "printf 'Windows host address: %s\\n' \"$windows_host\"\n"
               "printf 'QGC_WINDOWS_HOST=%s\\n' \"$windows_host\"\n"
               "printf 'ArduPilot path: %s\\n' \"$ardupilot_dir\"\n")
        .arg(_shellQuote(_ardupilotPath));
}

QString TrainingSimulatorController::_buildPlaneCommand() const
{
    const QString location = QStringLiteral("%1,%2,%3,%4")
                                 .arg(QString::number(_latitude, 'f', 8),
                                      QString::number(_longitude, 'f', 8),
                                      QString::number(_altitude, 'f', 2),
                                      QString::number(_heading, 'f', 1));
    const QString wipeArgument = _wipeEeprom ? QStringLiteral(" -w") : QString();
    return QStringLiteral(
               "set -e\n"
               "cd -- %1\n"
               "unset DISPLAY WAYLAND_DISPLAY\n"
               "networking_mode=unknown\n"
               "if command -v wslinfo >/dev/null 2>&1; then networking_mode=\"$(wslinfo --networking-mode 2>/dev/null || echo unknown)\"; fi\n"
               "if [ \"$networking_mode\" = mirrored ]; then gateway=127.0.0.1; else "
               "gateway=\"$(ip -4 route show default 2>/dev/null | sed -n 's/^default via \\([^ ]*\\).*/\\1/p' | head -n 1)\"; "
               "if [ -z \"$gateway\" ]; then gateway=\"$(awk '/^nameserver[[:space:]]+/ { print $2; exit }' "
               "/etc/resolv.conf 2>/dev/null)\"; fi; fi\n"
               "if [ -z \"$gateway\" ]; then echo 'Cannot determine the Windows host address.' >&2; exit 3; fi\n"
               "echo \"QGC Windows host IP: $gateway\"\n"
               "param_file=/tmp/qgc_training_follow.params\n"
               "pid_file=%2\n"
               "printf 'FOLL_ENABLE,1\\nFT_SYSID,255\\n' > \"$param_file\"\n"
               "rm -f \"$pid_file\"\n"
               "printf '%s\\n' \"$$\" > \"$pid_file\"\n"
               "trap 'rm -f \"$pid_file\"' EXIT\n"
               "./waf configure --board sitl\n"
               "./waf build --target bin/arduplane\n"
               "exec build/sitl/bin/arduplane -S%3 --model quadplane --speedup 1 --sysid 1 --slave 0 "
               "--defaults \"Tools/autotest/default_params/quadplane.parm,$param_file\" "
               "--sim-address 127.0.0.1 -I0 --home %4 --serial0 \"udpclient:$gateway:14550\"\n")
        .arg(_shellQuote(_ardupilotPath), _shellQuote(QString::fromLatin1(kPidFile)), wipeArgument, _shellQuote(location));
}

QString TrainingSimulatorController::_shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}
