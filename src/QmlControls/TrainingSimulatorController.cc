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
#include "LinkManager.h"
#include "SettingsManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtMath>

#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr double kEarthRadiusMeters = 6378137.0;
constexpr double kKnotsPerMeterPerSecond = 1.9438444924406048;

double courseFromVelocity(double northVelocity, double eastVelocity)
{
    if (std::hypot(northVelocity, eastVelocity) < 0.01) {
        return 0.0;
    }
    double course = qRadiansToDegrees(std::atan2(eastVelocity, northVelocity));
    if (course < 0.0) {
        course += 360.0;
    }
    return course;
}
}

TrainingSimulatorController::TrainingSimulatorController(QObject *parent)
    : QObject(parent)
{
    _loadConfiguration();
    _planeProcess.setProcessChannelMode(QProcess::MergedChannels);
    _targetTimer.setTimerType(Qt::PreciseTimer);

    connect(&_planeProcess, &QProcess::readyRead, this, [this]() {
        _appendLog(tr("飞机"), _planeProcess.readAll());
    });
    connect(&_planeProcess, &QProcess::stateChanged, this, &TrainingSimulatorController::runningChanged);
    connect(&_targetTimer, &QTimer::timeout, this, &TrainingSimulatorController::_sendTargetPosition);

    connect(&_planeProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!_stopping && (error == QProcess::FailedToStart || error == QProcess::Crashed)) {
            _setError(tr("飞机仿真进程错误：%1").arg(_planeProcess.errorString()));
        }
    });
    connect(&_planeProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        _handleUnexpectedExit(exitCode);
    });

    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, &TrainingSimulatorController::stopTraining, Qt::DirectConnection);
    }

    // A Windows Job Object terminates SITL if QGC is killed. The marker is
    // retained only so QGC settings can be restored after an abnormal exit.
    if (_cleanupRequired || _qgcConfigurationOverridden) {
        QTimer::singleShot(0, this, [this]() {
            _restoreQgcConfiguration();
            _setCleanupRequired(false);
            _appendLog(tr("系统"), tr("已恢复上次异常退出前的 QGC 仿真设置。\n").toUtf8());
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
    return _targetTimer.isActive();
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
        return tr("飞机仿真正在运行，小车仿真已停止");
    }
    if (targetRunning()) {
        return tr("小车仿真正在运行，飞机 SITL 已停止");
    }
    return tr("仿真未启动");
}

void TrainingSimulatorController::_loadConfiguration()
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    _sitlExecutable = settings.value(QStringLiteral("sitlExecutable"), _sitlExecutable).toString();
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

DEFINE_STRING_SETTER(SitlExecutable, _sitlExecutable, "sitlExecutable")
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

bool TrainingSimulatorController::_validateLocation()
{
    if (_latitude <= -89.999 || _latitude >= 89.999 || _longitude < -180.0 || _longitude > 180.0) {
        _setError(tr("起始点经纬度超出有效范围。"));
        return false;
    }
    if (_altitude < -1000.0 || _altitude > 100000.0) {
        _setError(tr("海拔高度超出有效范围。"));
        return false;
    }
    return true;
}

bool TrainingSimulatorController::_validateTargetConfiguration()
{
    if (!_validateLocation()) {
        return false;
    }
    if (_targetSpeed < 0.0 || _targetRate <= 0.0 || _targetRate > 100.0 || _targetRadius < 1.0) {
        _setError(tr("小车速度、输出频率或转弯半径设置无效；输出频率上限为 100 Hz。"));
        return false;
    }
    if (_nmeaPort < 1 || _nmeaPort > 65535) {
        _setError(tr("NMEA UDP 端口必须在 1 到 65535 之间。"));
        return false;
    }
    const QStringList patterns{QStringLiteral("circle"), QStringLiteral("east"),
                               QStringLiteral("north"), QStringLiteral("line")};
    if (!patterns.contains(_targetPattern)) {
        _setError(tr("小车运动轨迹设置无效。"));
        return false;
    }
    return true;
}

bool TrainingSimulatorController::startPlaneSimulation()
{
#ifndef Q_OS_WIN
    _setError(tr("该仿真运行包当前仅支持 Windows。"));
    return false;
#else
    if (planeRunning()) {
        _setError(tr("飞机仿真已经启动。"));
        return false;
    }
    if (!_validateLocation()) {
        return false;
    }
    if (_heading < 0.0 || _heading >= 360.0) {
        _setError(tr("飞机初始航向应为 0（含）到 360（不含）度。"));
        return false;
    }

    _setError(QString());
    if (!running()) {
        clearLog();
    }
    QString executable;
    QString quadplaneDefaults;
    QString followDefaults;
    QString runtimeDirectory;
    if (!_prepareEnvironment(executable, quadplaneDefaults, followDefaults, runtimeDirectory)) {
        return false;
    }

    _configureQgcForTraining();
    _appendLog(tr("系统"), tr("已启用飞机所需的 MAVLink UDP 14550 和 FOLLOW_TARGET。\n").toUtf8());

    QStringList arguments{QStringLiteral("-S")};
    if (_wipeEeprom) {
        arguments << QStringLiteral("-w");
    }
    const QString location = QStringLiteral("%1,%2,%3,%4")
                                 .arg(QString::number(_latitude, 'f', 8),
                                      QString::number(_longitude, 'f', 8),
                                      QString::number(_altitude, 'f', 2),
                                      QString::number(_heading, 'f', 1));
    arguments << QStringLiteral("--model") << QStringLiteral("quadplane")
              << QStringLiteral("--speedup") << QStringLiteral("1")
              << QStringLiteral("--sysid") << QStringLiteral("1")
              << QStringLiteral("--slave") << QStringLiteral("0")
              << QStringLiteral("--defaults") << (quadplaneDefaults + QLatin1Char(',') + followDefaults)
              << QStringLiteral("--sim-address") << QStringLiteral("127.0.0.1")
              << QStringLiteral("-I0")
              << QStringLiteral("--home") << location
              << QStringLiteral("--serial0") << QStringLiteral("udpclient:127.0.0.1:14550")
              << QStringLiteral("--serial1") << QStringLiteral("none")
              << QStringLiteral("--serial2") << QStringLiteral("none");

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString binaryDirectory = QFileInfo(executable).absolutePath();
    environment.insert(QStringLiteral("PATH"), binaryDirectory + QDir::listSeparator()
                                                + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("CYGWIN"), QStringLiteral("nodosfilewarning"));
    _planeProcess.setProcessEnvironment(environment);
    _planeProcess.setProgram(executable);
    _planeProcess.setArguments(arguments);
    _planeProcess.setWorkingDirectory(runtimeDirectory);

    _stopping = true; // Suppress the generic exit handler during startup validation.
    _planeProcess.start();
    if (!_planeProcess.waitForStarted(5000)) {
        _stopping = false;
        _setError(tr("飞机 SITL 启动失败：%1").arg(_planeProcess.errorString()));
        if (!targetRunning()) {
            _restoreQgcConfiguration();
        }
        return false;
    }
    if (_planeProcess.waitForFinished(750)) {
        const QString output = QString::fromUtf8(_planeProcess.readAll()).trimmed();
        _stopping = false;
        _setError(tr("飞机 SITL 启动后立即退出（退出码 %1）：%2")
                      .arg(_planeProcess.exitCode()).arg(output));
        if (!targetRunning()) {
            _restoreQgcConfiguration();
        }
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const bool configured = SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                                        &limits, sizeof(limits));
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                     static_cast<DWORD>(_planeProcess.processId()));
        const bool assigned = process && AssignProcessToJobObject(job, process);
        if (process) {
            CloseHandle(process);
        }
        if (configured && assigned) {
            _planeJobHandle = reinterpret_cast<quintptr>(job);
        } else {
            CloseHandle(job);
            _appendLog(tr("系统"), tr("警告：无法把 SITL 加入 Windows 作业对象；正常退出时仍会清理进程。\n").toUtf8());
        }
    }
    _stopping = false;

    _setCleanupRequired(true);
    _appendLog(tr("系统"), tr("飞机仿真已启动：直接运行预编译 ArduPlane SITL，未使用 WSL、Python 或源码编译。\n").toUtf8());
    emit runningChanged();
    return true;
#endif
}

bool TrainingSimulatorController::startTargetSimulation()
{
#ifndef Q_OS_WIN
    _setError(tr("该仿真运行包当前仅支持 Windows。"));
    return false;
#else
    if (targetRunning()) {
        _setError(tr("小车仿真已经启动。"));
        return false;
    }
    if (!_validateTargetConfiguration()) {
        return false;
    }
    _setError(QString());
    if (!running()) {
        clearLog();
    }
    _configureQgcForTraining();
    _targetElapsed.start();
    _lastTargetLogSecond = -1;
    _targetTimer.start(qMax(1, qRound(1000.0 / _targetRate)));
    _sendTargetPosition();
    if (!targetRunning()) {
        if (!planeRunning()) {
            _restoreQgcConfiguration();
        }
        return false;
    }
    _setCleanupRequired(true);
    _appendLog(tr("系统"), tr("小车仿真已启动：通过 NMEA UDP %1 输出目标位置。\n")
                                  .arg(_nmeaPort).toUtf8());
    emit runningChanged();
    return true;
#endif
}

void TrainingSimulatorController::stopTraining()
{
    if (_stopping) {
        return;
    }
    _stopping = true;
    _stopTargetSimulation();
    _stopPlaneProcess();
    _restoreQgcConfiguration();
    _setCleanupRequired(false);
    _stopping = false;
    emit runningChanged();
}

void TrainingSimulatorController::stopPlaneSimulation()
{
    if (_stopping || !planeRunning()) {
        return;
    }
    _stopping = true;
    _stopPlaneProcess();
    _stopping = false;
    _appendLog(tr("系统"), targetRunning()
        ? tr("飞机仿真已停止，小车仿真继续运行。\n").toUtf8()
        : tr("飞机仿真已停止。\n").toUtf8());
    if (!targetRunning()) {
        _restoreQgcConfiguration();
        _setCleanupRequired(false);
    }
    emit runningChanged();
}

void TrainingSimulatorController::stopTargetSimulation()
{
    if (_stopping || !targetRunning()) {
        return;
    }
    _stopTargetSimulation();
    _appendLog(tr("系统"), planeRunning()
        ? tr("小车仿真已停止，飞机仿真继续运行。\n").toUtf8()
        : tr("小车仿真已停止。\n").toUtf8());
    if (!planeRunning()) {
        _restoreQgcConfiguration();
        _setCleanupRequired(false);
    }
    emit runningChanged();
}

void TrainingSimulatorController::_stopTargetSimulation()
{
    _targetTimer.stop();
    _targetSocket.close();
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
        LinkManager::instance()->connectNmeaSource();
        return;
    }
    LinkManager::instance()->disconnectNmeaSource();
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
    app->followTarget()->setRawValue(1);
    // Starting the training simulator is an explicit connection action. Normal
    // NMEA setting changes remain disconnected until the user presses Connect.
    LinkManager::instance()->connectNmeaSource();
}

void TrainingSimulatorController::_restoreQgcConfiguration()
{
    if (!_qgcConfigurationOverridden) {
        return;
    }
    LinkManager::instance()->disconnectNmeaSource();
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

void TrainingSimulatorController::_stopPlaneProcess()
{
    if (_planeProcess.state() != QProcess::NotRunning) {
        _planeProcess.terminate();
        if (!_planeProcess.waitForFinished(2000)) {
            _planeProcess.kill();
            _planeProcess.waitForFinished(1500);
        }
    }
#ifdef Q_OS_WIN
    if (_planeJobHandle != 0) {
        CloseHandle(reinterpret_cast<HANDLE>(_planeJobHandle));
        _planeJobHandle = 0;
    }
#endif
}

void TrainingSimulatorController::_handleUnexpectedExit(int exitCode)
{
    if (_stopping) {
        emit runningChanged();
        return;
    }
    _stopping = true;
    _stopPlaneProcess();
    _stopping = false;
    _setError(tr("飞机仿真意外退出（退出码 %1）。小车仿真状态不受影响，请查看运行日志。")
                  .arg(exitCode));
    if (!targetRunning()) {
        _restoreQgcConfiguration();
        _setCleanupRequired(false);
    }
    emit runningChanged();
}

QString TrainingSimulatorController::_resolveSitlExecutable() const
{
    QStringList candidates;
    if (!_sitlExecutable.trimmed().isEmpty()) {
        candidates << _sitlExecutable.trimmed();
    }
    const QString environmentExecutable = qEnvironmentVariable("AEROFOLLOW_SITL_EXE").trimmed();
    if (!environmentExecutable.isEmpty()) {
        candidates << environmentExecutable;
    }
    const QString appDirectory = QCoreApplication::applicationDirPath();
    candidates << QDir(appDirectory).filePath(QStringLiteral("simulator/bin/arduplane.exe"))
               << QDir(appDirectory).filePath(QStringLiteral("../simulator/bin/arduplane.exe"));

    for (const QString &candidate : candidates) {
        const QFileInfo info(QDir::cleanPath(candidate));
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

QString TrainingSimulatorController::_runtimeRootForExecutable(const QString &executable) const
{
    QDir binaryDirectory = QFileInfo(executable).absoluteDir();
    if (binaryDirectory.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
        binaryDirectory.cdUp();
    }
    return binaryDirectory.absolutePath();
}

bool TrainingSimulatorController::_prepareEnvironment(QString &executable, QString &quadplaneDefaults,
                                                       QString &followDefaults, QString &runtimeDirectory)
{
    executable = _resolveSitlExecutable();
    if (executable.isEmpty()) {
        _setError(tr("未找到预编译飞机仿真程序。安装包必须包含 simulator\\bin\\arduplane.exe，"
                     "开发环境也可设置 AEROFOLLOW_SITL_EXE。"));
        return false;
    }

    const QString packageRoot = _runtimeRootForExecutable(executable);
    quadplaneDefaults = QDir(packageRoot).filePath(QStringLiteral("params/quadplane.parm"));
    followDefaults = QDir(packageRoot).filePath(QStringLiteral("params/aerofollow.parm"));
    const QString cygwinRuntime = QFileInfo(executable).absoluteDir().filePath(QStringLiteral("cygwin1.dll"));
    QStringList missing;
    for (const QString &path : {quadplaneDefaults, followDefaults, cygwinRuntime}) {
        if (!QFileInfo::exists(path)) {
            missing << QDir::toNativeSeparators(path);
        }
    }
    if (!missing.isEmpty()) {
        _setError(tr("飞机仿真运行包不完整，缺少：%1").arg(missing.join(QStringLiteral("；"))));
        return false;
    }

    runtimeDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                       + QStringLiteral("/training/sitl");
    if (!QDir().mkpath(runtimeDirectory)) {
        _setError(tr("无法创建 SITL 可写运行目录：%1").arg(QDir::toNativeSeparators(runtimeDirectory)));
        return false;
    }

    _appendLog(tr("环境"), tr("SITL：%1\n参数：%2；%3\n运行目录：%4\n")
                                  .arg(QDir::toNativeSeparators(executable),
                                       QDir::toNativeSeparators(quadplaneDefaults),
                                       QDir::toNativeSeparators(followDefaults),
                                       QDir::toNativeSeparators(runtimeDirectory)).toUtf8());
    _appendLog(tr("环境"), tr("运行包已就绪：客户机不需要 WSL、ArduPilot 源码、Python 或编译工具。\n").toUtf8());
    return true;
}

QByteArray TrainingSimulatorController::_nmeaSentence(const QString &body)
{
    const QByteArray bytes = body.toLatin1();
    quint8 checksum = 0;
    for (const char value : bytes) {
        checksum ^= static_cast<quint8>(value);
    }
    return QByteArray("$") + bytes + QByteArray("*")
           + QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper() + QByteArray("\r\n");
}

QString TrainingSimulatorController::_nmeaCoordinate(double value, bool latitude, QChar &direction)
{
    direction = value < 0.0 ? (latitude ? QLatin1Char('S') : QLatin1Char('W'))
                            : (latitude ? QLatin1Char('N') : QLatin1Char('E'));
    const double absoluteValue = std::abs(value);
    const int degrees = static_cast<int>(absoluteValue);
    const double minutes = (absoluteValue - degrees) * 60.0;
    return QStringLiteral("%1%2")
        .arg(degrees, latitude ? 2 : 3, 10, QLatin1Char('0'))
        .arg(minutes, 11, 'f', 8, QLatin1Char('0'));
}

void TrainingSimulatorController::_sendTargetPosition()
{
    if (!_targetElapsed.isValid()) {
        return;
    }
    const double elapsed = _targetElapsed.nsecsElapsed() / 1000000000.0;
    double north = 0.0;
    double east = 0.0;
    double northVelocity = 0.0;
    double eastVelocity = 0.0;

    if (_targetPattern == QStringLiteral("circle")) {
        const double angularRate = _targetSpeed / qMax(_targetRadius, 1.0);
        const double theta = angularRate * elapsed;
        north = _targetRadius * std::sin(theta);
        east = _targetRadius * std::cos(theta);
        northVelocity = _targetSpeed * std::cos(theta);
        eastVelocity = -_targetSpeed * std::sin(theta);
    } else if (_targetPattern == QStringLiteral("east") || _targetPattern == QStringLiteral("north")) {
        const double heading = _targetPattern == QStringLiteral("east") ? 90.0 : 0.0;
        const double headingRadians = qDegreesToRadians(heading);
        northVelocity = _targetSpeed * std::cos(headingRadians);
        eastVelocity = _targetSpeed * std::sin(headingRadians);
        north = northVelocity * elapsed;
        east = eastVelocity * elapsed;
    } else {
        constexpr double lineLength = 80.0;
        const double period = (2.0 * lineLength) / qMax(_targetSpeed, 0.01);
        const double phase = std::fmod(elapsed, period);
        const bool outbound = phase <= period / 2.0;
        const double distance = outbound ? phase * _targetSpeed
                                         : lineLength - ((phase - period / 2.0) * _targetSpeed);
        east = distance;
        eastVelocity = _targetSpeed * (outbound ? 1.0 : -1.0);
    }

    const double targetLatitude = _latitude + qRadiansToDegrees(north / kEarthRadiusMeters);
    const double targetLongitude = _longitude
        + qRadiansToDegrees(east / (kEarthRadiusMeters * std::cos(qDegreesToRadians(_latitude))));
    const double speed = std::hypot(northVelocity, eastVelocity);
    const double course = courseFromVelocity(northVelocity, eastVelocity);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString time = now.toString(QStringLiteral("hhmmss"))
                         + QStringLiteral(".%1").arg(now.time().msec() / 10, 2, 10, QLatin1Char('0'));
    const QString date = now.toString(QStringLiteral("ddMMyy"));
    QChar latitudeDirection;
    QChar longitudeDirection;
    const QString latitude = _nmeaCoordinate(targetLatitude, true, latitudeDirection);
    const QString longitude = _nmeaCoordinate(targetLongitude, false, longitudeDirection);

    const QString gga = QStringLiteral("GNGGA,%1,%2,%3,%4,%5,4,22,0.8,%6,M,-4.0100,M,,")
                            .arg(time).arg(latitude).arg(latitudeDirection).arg(longitude).arg(longitudeDirection)
                            .arg(_altitude, 0, 'f', 4);
    const QString rmc = QStringLiteral("GNRMC,%1,A,%2,%3,%4,%5,%6,%7,%8,5.8,W,A,S")
                            .arg(time).arg(latitude).arg(latitudeDirection).arg(longitude).arg(longitudeDirection)
                            .arg(speed * kKnotsPerMeterPerSecond, 0, 'f', 3)
                            .arg(course, 0, 'f', 1)
                            .arg(date);
    const QByteArray packet = _nmeaSentence(gga) + _nmeaSentence(rmc);
    if (_targetSocket.writeDatagram(packet, QHostAddress::LocalHost, static_cast<quint16>(_nmeaPort)) != packet.size()) {
        _setError(tr("小车 NMEA UDP 发送失败：%1").arg(_targetSocket.errorString()));
        QTimer::singleShot(0, this, &TrainingSimulatorController::stopTargetSimulation);
        return;
    }

    const qint64 elapsedSecond = static_cast<qint64>(elapsed);
    if (elapsedSecond != _lastTargetLogSecond) {
        _lastTargetLogSecond = elapsedSecond;
        _appendLog(tr("小车"), tr("t=%1s lat=%2 lon=%3 alt=%4m speed=%5m/s course=%6deg\n")
                                      .arg(elapsed, 0, 'f', 1)
                                      .arg(targetLatitude, 0, 'f', 8)
                                      .arg(targetLongitude, 0, 'f', 8)
                                      .arg(_altitude, 0, 'f', 1)
                                      .arg(speed, 0, 'f', 2)
                                      .arg(course, 0, 'f', 1).toUtf8());
    }
}
