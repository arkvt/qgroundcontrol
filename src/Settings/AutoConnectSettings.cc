/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AutoConnectSettings.h"
#include "LinkManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
#include <QtQml/QQmlEngine>

DECLARE_SETTINGGROUP(AutoConnect, "AutoConnect")
{
    qmlRegisterUncreatableType<AutoConnectSettings>("QGroundControl.SettingsManager", 1, 0, "AutoConnectSettings", "Reference only");

    // Settings group name was changed from "LinkManager" to "AutoConnect" in v5.0.0
    // Copy over an old settings to the new name
    QSettings settings;
    static const char* deprecatedGroupName = "LinkManager";
    if (settings.childGroups().contains(deprecatedGroupName)) {
        settings.beginGroup(deprecatedGroupName);
        QList<QPair<QString, QVariant>> values;
        for (const QString& key: settings.childKeys()) {
            values.append(QPair<QString, QVariant>(key, settings.value(key)));
        }
        settings.endGroup();
        settings.remove(deprecatedGroupName);

        settings.beginGroup(_name);
        for (const QPair<QString, QVariant>& pair: values) {
            settings.setValue(pair.first, pair.second);
        }
        settings.endGroup();
    }

    // NMEA device choices are translated for display in QML, but the stored
    // values are protocol identifiers consumed by LinkManager. Migrate values
    // saved by older builds which persisted the translated display text.
    settings.beginGroup(_name);
    const QString nmeaPortValue = settings.value(QStringLiteral("autoConnectNmeaPort")).toString();
    const QString translatedDisabled = QCoreApplication::translate("LinkSettings", "Disabled");
    const QString translatedUdpPort = QCoreApplication::translate("LinkSettings", "UDP Port");
    if ((nmeaPortValue == translatedDisabled) && (nmeaPortValue != QStringLiteral("Disabled"))) {
        settings.setValue(QStringLiteral("autoConnectNmeaPort"), QStringLiteral("Disabled"));
    } else if ((nmeaPortValue == translatedUdpPort) && (nmeaPortValue != QStringLiteral("UDP Port"))) {
        settings.setValue(QStringLiteral("autoConnectNmeaPort"), QStringLiteral("UDP Port"));
    }
    settings.endGroup();
}

DECLARE_SETTINGSFACT(AutoConnectSettings, autoConnectUDP)
DECLARE_SETTINGSFACT(AutoConnectSettings, udpListenPort)
DECLARE_SETTINGSFACT(AutoConnectSettings, udpTargetHostIP)
DECLARE_SETTINGSFACT(AutoConnectSettings, udpTargetHostPort)
DECLARE_SETTINGSFACT(AutoConnectSettings, nmeaUdpPort)

DECLARE_SETTINGSFACT_NO_FUNC(AutoConnectSettings, autoConnectPixhawk)
{
    if (!_autoConnectPixhawkFact) {
        _autoConnectPixhawkFact = _createSettingsFact(autoConnectPixhawkName);
#ifdef Q_OS_IOS
        _autoConnectPixhawkFact->setVisible(false);
#endif
    }
    return _autoConnectPixhawkFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(AutoConnectSettings, autoConnectSiKRadio)
{
    if (!_autoConnectSiKRadioFact) {
        _autoConnectSiKRadioFact = _createSettingsFact(autoConnectSiKRadioName);
#ifdef Q_OS_IOS
        _autoConnectSiKRadioFact->setVisible(false);
#endif
    }
    return _autoConnectSiKRadioFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(AutoConnectSettings, autoConnectRTKGPS)
{
    if (!_autoConnectRTKGPSFact) {
        _autoConnectRTKGPSFact = _createSettingsFact(autoConnectRTKGPSName);
#ifdef Q_OS_IOS
        _autoConnectRTKGPSFact->setVisible(false);
#endif
    }
    return _autoConnectRTKGPSFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(AutoConnectSettings, autoConnectLibrePilot)
{
    if (!_autoConnectLibrePilotFact) {
        _autoConnectLibrePilotFact = _createSettingsFact(autoConnectLibrePilotName);
#ifdef Q_OS_IOS
        _autoConnectLibrePilotFact->setVisible(false);
#endif
    }
    return _autoConnectLibrePilotFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(AutoConnectSettings, autoConnectNmeaPort)
{
    if (!_autoConnectNmeaPortFact) {
        _autoConnectNmeaPortFact = _createSettingsFact(autoConnectNmeaPortName);
#ifdef Q_OS_IOS
        _autoConnectNmeaPortFact->setVisible(false);
#endif
    }
    return _autoConnectNmeaPortFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(AutoConnectSettings, autoConnectNmeaBaud)
{
    if (!_autoConnectNmeaBaudFact) {
        _autoConnectNmeaBaudFact = _createSettingsFact(autoConnectNmeaBaudName);
#ifdef Q_OS_IOS
        _autoConnectNmeaBaudFact->setVisible(false);
#endif
    }
    return _autoConnectNmeaBaudFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(AutoConnectSettings, autoConnectZeroConf)
{
    if (!_autoConnectZeroConfFact) {
        _autoConnectZeroConfFact = _createSettingsFact(autoConnectZeroConfName);
#ifdef Q_OS_IOS
        _autoConnectZeroConfFact->setVisible(false);
#endif
    }
    return _autoConnectZeroConfFact;
}
