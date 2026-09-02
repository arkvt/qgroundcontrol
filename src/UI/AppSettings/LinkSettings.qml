/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.ScreenTools
import QGroundControl.Palette

SettingsPage {
    property var _linkManager:          QGroundControl.linkManager
    property var _autoConnectSettings:  QGroundControl.settingsManager.autoConnectSettings

    SettingsGroupLayout {
        heading:        qsTr("AutoConnect")
        visible:        _autoConnectSettings.visible

        Repeater {
            id: autoConnectRepeater

            model: [
                _autoConnectSettings.autoConnectPixhawk,
                _autoConnectSettings.autoConnectSiKRadio,
                _autoConnectSettings.autoConnectLibrePilot,
                _autoConnectSettings.autoConnectUDP,
                _autoConnectSettings.autoConnectZeroConf,
                _autoConnectSettings.autoConnectRTKGPS,
            ]

            property var names: [ qsTr("Pixhawk"), qsTr("SiK Radio"), qsTr("LibrePilot"), qsTr("UDP"), qsTr("Zero-Conf"), qsTr("RTK") ]

            FactCheckBoxSlider {
                Layout.fillWidth:   true
                text:               autoConnectRepeater.names[index]
                fact:               modelData
                visible:            modelData.visible
            }
        }
    }

    SettingsGroupLayout {
        heading: qsTr("NMEA GPS")
        visible: QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaPort.visible && QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaBaud.visible

        LabelledComboBox {
            id: nmeaPortCombo
            label: qsTr("Device")

            readonly property string disabledSettingValue: "Disabled"
            readonly property string udpSettingValue:      "UDP Port"
            readonly property string disabledDisplayText:  qsTr("Disabled")
            readonly property string udpDisplayText:       qsTr("UDP Port")

            model: ListModel {}

            function settingValueForDisplayText(displayText) {
                if (displayText === disabledDisplayText) {
                    return disabledSettingValue
                }
                if (displayText === udpDisplayText) {
                    return udpSettingValue
                }
                return displayText
            }

            function displayTextForSettingValue(settingValue) {
                if (settingValue === disabledSettingValue) {
                    return disabledDisplayText
                }
                if (settingValue === udpSettingValue) {
                    return udpDisplayText
                }
                return settingValue
            }

            onActivated: (index) => {
                if (index !== -1) {
                    const displayText = comboBox.textAt(index)
                    QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaPort.value = settingValueForDisplayText(displayText)
                }
            }

            Component.onCompleted: {
                var model = []

                model.push(disabledDisplayText)
                model.push(udpDisplayText)

                if (QGroundControl.linkManager.serialPorts.length === 0) {
                    model.push(qsTr("Serial <none available>"))
                } else {
                    for (var i in QGroundControl.linkManager.serialPorts) {
                        model.push(QGroundControl.linkManager.serialPorts[i])
                    }
                }
                nmeaPortCombo.model = model

                const settingsFact = QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaPort
                const settingValue = settingValueForDisplayText(settingsFact.valueString)
                if (settingValue !== settingsFact.valueString) {
                    settingsFact.value = settingValue
                }

                const index = nmeaPortCombo.comboBox.find(displayTextForSettingValue(settingValue))
                nmeaPortCombo.currentIndex = index
            }
        }

        LabelledComboBox {
            id: nmeaBaudCombo
            visible: (nmeaPortCombo.currentText !== nmeaPortCombo.udpDisplayText) && (nmeaPortCombo.currentText !== nmeaPortCombo.disabledDisplayText)
            label: qsTr("Baudrate")
            model: QGroundControl.linkManager.serialBaudRates

            onActivated: (index) => {
                if (index !== -1) {
                    QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaBaud.value = parseInt(comboBox.textAt(index));
                }
            }

            Component.onCompleted: {
                const index = nmeaBaudCombo.comboBox.find(QGroundControl.settingsManager.autoConnectSettings.autoConnectNmeaBaud.valueString);
                nmeaBaudCombo.currentIndex = index;
            }
        }

        LabelledFactTextField {
            visible: nmeaPortCombo.currentText === nmeaPortCombo.udpDisplayText
            label: qsTr("NMEA stream UDP port")
            fact: QGroundControl.settingsManager.autoConnectSettings.nmeaUdpPort
        }

        LabelledButton {
            label:      qsTr("Raw GNSS data")
            buttonText: qsTr("View")
            onClicked:  gnssDataDialogComponent.createObject(mainWindow).open()
        }
    }

    SettingsGroupLayout {
        heading: qsTr("Links")

        Repeater {
            model: _linkManager.linkConfigurations

            RowLayout {
                Layout.fillWidth:   true
                visible:            !object.dynamic

                QGCLabel {
                    Layout.fillWidth:   true
                    text:               object.name
                }
                QGCColoredImage {
                    height:                 ScreenTools.minTouchPixels
                    width:                  height
                    sourceSize.height:      height
                    fillMode:               Image.PreserveAspectFit
                    mipmap:                 true
                    smooth:                 true
                    color:                  qgcPalEdit.text
                    source:                 "/res/pencil.svg"
                    enabled:                !object.link

                    QGCPalette {
                        id: qgcPalEdit
                        colorGroupEnabled: parent.enabled
                    }

                    QGCMouseArea {
                        fillItem: parent
                        onClicked: {
                            var editingConfig = _linkManager.startConfigurationEditing(object)
                            linkDialogComponent.createObject(mainWindow, { editingConfig: editingConfig, originalConfig: object }).open()
                        }
                    }
                }
                QGCColoredImage {
                    height:                 ScreenTools.minTouchPixels
                    width:                  height
                    sourceSize.height:      height
                    fillMode:               Image.PreserveAspectFit
                    mipmap:                 true
                    smooth:                 true
                    color:                  qgcPalDelete.text
                    source:                 "/res/TrashDelete.svg"

                    QGCPalette {
                        id: qgcPalDelete
                        colorGroupEnabled: parent.enabled
                    }

                    QGCMouseArea {
                        fillItem:   parent
                        onClicked:  mainWindow.showMessageDialog(
                                        qsTr("Delete Link"), 
                                        qsTr("Are you sure you want to delete '%1'?").arg(object.name), 
                                        Dialog.Ok | Dialog.Cancel, 
                                        function () {
                                            _linkManager.removeConfiguration(object)
                                        })
                    }
                }
                QGCButton {
                    text:       object.link ? qsTr("Disconnect") : qsTr("Connect")
                    onClicked: {
                        if (object.link) {
                            object.link.disconnect()
                        } else {
                            _linkManager.createConnectedLink(object)
                        }
                    }
                }
            }
        }

        LabelledButton {
            label:      qsTr("Add New Link")
            buttonText: qsTr("Add")

            onClicked: {
                var editingConfig = _linkManager.createConfiguration(ScreenTools.isSerialAvailable ? LinkConfiguration.TypeSerial : LinkConfiguration.TypeUdp, "")
                linkDialogComponent.createObject(mainWindow, { editingConfig: editingConfig, originalConfig: null }).open()
            }
        }
    }

    Component {
        id: gnssDataDialogComponent

        QGCPopupDialog {
            id:      gnssDataDialog
            title:   qsTr("GNSS Data")
            buttons: Dialog.Close

            property var _positionManager: QGroundControl.qgcPositionManger
            property bool _paused: false
            property string _displayedData: _positionManager.nmeaRawData
            property bool _connectionRequested: _linkManager.nmeaConnectionRequested

            function updateDisplayedData() {
                if (!_paused) {
                    _displayedData = _positionManager.nmeaRawData
                    Qt.callLater(function() {
                        gnssDataText.cursorPosition = gnssDataText.length
                    })
                }
            }

            Connections {
                target: gnssDataDialog._positionManager

                function onNmeaRawDataChanged() {
                    gnssDataDialog.updateDisplayedData()
                }
            }

            ColumnLayout {
                spacing: ScreenTools.defaultFontPixelHeight / 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth

                    QGCLabel {
                        Layout.fillWidth: true
                        text: gnssDataDialog._positionManager.nmeaSourceActive
                              ? qsTr("GNSS source connected")
                              : gnssDataDialog._connectionRequested
                                ? qsTr("Connecting to GNSS source")
                                : qsTr("GNSS source disconnected")
                        color: gnssDataDialog._positionManager.nmeaSourceActive
                               ? QGroundControl.globalPalette.colorGreen
                               : QGroundControl.globalPalette.text
                    }

                    QGCButton {
                        text: gnssDataDialog._positionManager.nmeaSourceActive || gnssDataDialog._connectionRequested
                              ? qsTr("Disconnect")
                              : qsTr("Connect")
                        primary: !gnssDataDialog._positionManager.nmeaSourceActive && !gnssDataDialog._connectionRequested
                        onClicked: {
                            if (gnssDataDialog._positionManager.nmeaSourceActive || gnssDataDialog._connectionRequested) {
                                _linkManager.disconnectNmeaSource()
                            } else {
                                _linkManager.connectNmeaSource()
                            }
                        }
                    }

                    QGCButton {
                        text:       gnssDataDialog._paused ? qsTr("Resume") : qsTr("Pause")
                        iconSource: gnssDataDialog._paused ? "/res/Play.svg" : "/res/Pause.svg"
                        onClicked: {
                            gnssDataDialog._paused = !gnssDataDialog._paused
                            gnssDataDialog.updateDisplayedData()
                        }
                    }

                    QGCButton {
                        text:       qsTr("Clear")
                        iconSource: "/res/TrashDelete.svg"
                        enabled:    gnssDataDialog._displayedData.length > 0 || gnssDataDialog._positionManager.nmeaRawData.length > 0
                        onClicked: {
                            gnssDataDialog._positionManager.clearNmeaRawData()
                            gnssDataDialog._displayedData = ""
                        }
                    }
                }

                QGCLabel {
                    Layout.fillWidth: true
                    visible: _linkManager.nmeaConnectionError !== ""
                    text: _linkManager.nmeaConnectionError
                    color: QGroundControl.globalPalette.warningText
                    wrapMode: Text.WordWrap
                }

                ScrollView {
                    Layout.preferredWidth:  Math.min(mainWindow.width * 0.8, ScreenTools.defaultFontPixelWidth * 100)
                    Layout.preferredHeight: Math.min(mainWindow.height * 0.65, ScreenTools.defaultFontPixelHeight * 28)
                    clip: true

                    TextArea {
                        id: gnssDataText
                        text: gnssDataDialog._displayedData
                        placeholderText: qsTr("Waiting for GNSS data")
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        font.family: ScreenTools.fixedFontFamily
                        color: QGroundControl.globalPalette.text
                    }
                }
            }
        }
    }

    Component {
        id: linkDialogComponent

        QGCPopupDialog {
            title:                  originalConfig ? qsTr("Edit Link") : qsTr("Add New Link")
            buttons:                Dialog.Save | Dialog.Cancel
            acceptButtonEnabled:    nameField.text !== ""

            property var originalConfig
            property var editingConfig

            onAccepted: {
                linkSettingsLoader.item.saveSettings()
                editingConfig.name = nameField.text
                if (originalConfig) {
                    _linkManager.endConfigurationEditing(originalConfig, editingConfig)
                } else {
                    // If it was edited, it's no longer "dynamic"
                    editingConfig.dynamic = false
                    _linkManager.endCreateConfiguration(editingConfig)
                }
            }

            onRejected: _linkManager.cancelConfigurationEditing(editingConfig)

            ColumnLayout {
                spacing: ScreenTools.defaultFontPixelHeight / 2

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCLabel { text: qsTr("Name") }
                    QGCTextField {
                        id:                 nameField
                        Layout.fillWidth:   true
                        text:               editingConfig.name
                        placeholderText:    qsTr("Enter name")
                    }
                }

                QGCCheckBoxSlider {
                    Layout.fillWidth:   true
                    text:               qsTr("Automatically Connect on Start")
                    checked:            editingConfig.autoConnect
                    onCheckedChanged:   editingConfig.autoConnect = checked
                }

                QGCCheckBoxSlider {
                    Layout.fillWidth:   true
                    text:               qsTr("High Latency")
                    checked:            editingConfig.highLatency
                    onCheckedChanged:   editingConfig.highLatency = checked
                }

                LabelledComboBox {
                    label:                  qsTr("Type")
                    enabled:                originalConfig == null
                    model:                  _linkManager.linkTypeStrings
                    Component.onCompleted:  comboBox.currentIndex = editingConfig.linkType

                    onActivated: (index) => {
                        if (index !== editingConfig.linkType) {
                            // Save current name
                            var name = nameField.text
                            // Create new link configuration
                            editingConfig = _linkManager.createConfiguration(index, name)
                        }
                    }
                }

                Loader {
                    id:     linkSettingsLoader
                    source: subEditConfig.settingsURL

                    property var subEditConfig:         editingConfig
                    property int _firstColumnWidth:     ScreenTools.defaultFontPixelWidth * 12
                    property int _secondColumnWidth:    ScreenTools.defaultFontPixelWidth * 30
                    property int _rowSpacing:           ScreenTools.defaultFontPixelHeight / 2
                    property int _colSpacing:           ScreenTools.defaultFontPixelWidth / 2
                }
            }
        }
    }
}
