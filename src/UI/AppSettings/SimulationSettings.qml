/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controllers
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

SettingsPage {
    id: root

    readonly property var _simulator: TrainingSimulator
    readonly property real _labelWidth: ScreenTools.defaultFontPixelWidth * 20
    readonly property bool _planeConfigurationEnabled: !_simulator.planeRunning
    readonly property bool _targetConfigurationEnabled: !_simulator.targetRunning

    function commitNumber(field, propertyName) {
        const value = Number(field.text)
        if (isFinite(value)) {
            _simulator[propertyName] = value
        } else {
            field.text = _simulator[propertyName].toString()
        }
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    SettingsGroupLayout {
        heading: qsTr("仿真训练")
        headingDescription: qsTr("飞机使用安装包内的预编译 ArduPlane SITL；小车位置由 QGC 内置模拟器生成，随后转换为飞机所需的 FOLLOW_TARGET 消息。飞机和小车仿真可以分别启动与停止。")

        QGCLabel {
            Layout.fillWidth: true
            text: _simulator.supported
                  ? qsTr("按需分别启动飞机和小车仿真。首次启动或擦除 EEPROM 后，飞机连接通常需要数十秒。")
                  : qsTr("当前平台不支持此功能；请在 Windows 环境中使用。")
            wrapMode: Text.WordWrap
            color: _simulator.supported ? qgcPal.text : qgcPal.warningText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth

            QGCButton {
                text: qsTr("启动小车仿真")
                enabled: _simulator.supported && !_simulator.targetRunning
                onClicked: _simulator.startTargetSimulation()
            }

            QGCButton {
                text: qsTr("启动飞机仿真")
                enabled: _simulator.supported && !_simulator.planeRunning
                onClicked: _simulator.startPlaneSimulation()
            }

            QGCButton {
                text: qsTr("停止小车仿真")
                enabled: _simulator.targetRunning
                onClicked: _simulator.stopTargetSimulation()
            }

            QGCButton {
                text: qsTr("停止飞机仿真")
                enabled: _simulator.planeRunning
                onClicked: _simulator.stopPlaneSimulation()
            }

            QGCButton {
                text: qsTr("停止全部仿真")
                enabled: _simulator.running
                onClicked: _simulator.stopTraining()
            }

        }

        QGCLabel {
            Layout.fillWidth: true
            text: _simulator.statusText
            color: _simulator.running ? qgcPal.colorGreen : qgcPal.text
            font.bold: true
            wrapMode: Text.WordWrap
        }

        QGCLabel {
            Layout.fillWidth: true
            visible: _simulator.errorText !== ""
            text: _simulator.errorText
            color: qgcPal.warningText
            wrapMode: Text.WordWrap
        }
    }

    SettingsGroupLayout {
        heading: qsTr("仿真场景")
        headingDescription: qsTr("飞机和小车使用同一个起始点。纬度、经度为十进制度，高度为海拔高度。")

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: ScreenTools.defaultFontPixelWidth
            rowSpacing: ScreenTools.defaultFontPixelHeight / 2

            QGCLabel { text: qsTr("纬度"); Layout.preferredWidth: root._labelWidth }
            QGCTextField {
                id: latitudeField
                Layout.fillWidth: true
                text: _simulator.latitude.toFixed(7)
                numericValuesOnly: true
                enabled: root._targetConfigurationEnabled
                onEditingFinished: root.commitNumber(latitudeField, "latitude")
            }
            QGCLabel { text: "°" }

            QGCLabel { text: qsTr("经度") }
            QGCTextField {
                id: longitudeField
                Layout.fillWidth: true
                text: _simulator.longitude.toFixed(7)
                numericValuesOnly: true
                enabled: root._targetConfigurationEnabled
                onEditingFinished: root.commitNumber(longitudeField, "longitude")
            }
            QGCLabel { text: "°" }

            QGCLabel { text: qsTr("海拔高度") }
            QGCTextField {
                id: altitudeField
                Layout.fillWidth: true
                text: _simulator.altitude.toString()
                numericValuesOnly: true
                enabled: root._targetConfigurationEnabled
                onEditingFinished: root.commitNumber(altitudeField, "altitude")
            }
            QGCLabel { text: qsTr("m") }

            QGCLabel { text: qsTr("飞机初始航向") }
            QGCTextField {
                id: headingField
                Layout.fillWidth: true
                text: _simulator.heading.toString()
                numericValuesOnly: true
                enabled: root._planeConfigurationEnabled
                onEditingFinished: root.commitNumber(headingField, "heading")
            }
            QGCLabel { text: qsTr("°（0 为正北）") }

            QGCLabel { text: qsTr("小车速度") }
            QGCTextField {
                id: speedField
                Layout.fillWidth: true
                text: _simulator.targetSpeed.toString()
                numericValuesOnly: true
                enabled: root._targetConfigurationEnabled
                onEditingFinished: root.commitNumber(speedField, "targetSpeed")
            }
            QGCLabel { text: qsTr("m/s") }

            QGCLabel { text: qsTr("小车轨迹") }
            QGCComboBox {
                id: patternCombo
                Layout.fillWidth: true
                textRole: "text"
                enabled: root._targetConfigurationEnabled
                model: ListModel {
                    ListElement { text: qsTr("绕圈"); value: "circle" }
                    ListElement { text: qsTr("向东直行"); value: "east" }
                    ListElement { text: qsTr("向北直行"); value: "north" }
                    ListElement { text: qsTr("东西往返"); value: "line" }
                }
                Component.onCompleted: {
                    for (let i = 0; i < model.count; ++i) {
                        if (model.get(i).value === _simulator.targetPattern) {
                            currentIndex = i
                            break
                        }
                    }
                }
                onActivated: (index) => _simulator.targetPattern = model.get(index).value
            }
            Item { }

            QGCLabel { text: qsTr("绕圈半径") }
            QGCTextField {
                id: radiusField
                Layout.fillWidth: true
                text: _simulator.targetRadius.toString()
                numericValuesOnly: true
                enabled: root._targetConfigurationEnabled && patternCombo.currentIndex === 0
                onEditingFinished: root.commitNumber(radiusField, "targetRadius")
            }
            QGCLabel { text: qsTr("m") }

            QGCLabel { text: qsTr("NMEA 输出频率") }
            QGCTextField {
                id: rateField
                Layout.fillWidth: true
                text: _simulator.targetRate.toString()
                numericValuesOnly: true
                enabled: root._targetConfigurationEnabled
                onEditingFinished: root.commitNumber(rateField, "targetRate")
            }
            QGCLabel { text: qsTr("Hz") }

            QGCLabel { text: qsTr("NMEA UDP 端口") }
            QGCTextField {
                id: portField
                Layout.fillWidth: true
                text: _simulator.nmeaPort.toString()
                numericValuesOnly: true
                enabled: root._targetConfigurationEnabled
                onEditingFinished: root.commitNumber(portField, "nmeaPort")
            }
            QGCLabel { text: qsTr("默认 10110") }
        }

        QGCCheckBox {
            text: qsTr("启动飞机时擦除 SITL EEPROM（训练环境首次使用建议勾选）")
            checked: _simulator.wipeEeprom
            enabled: root._planeConfigurationEnabled
            onClicked: _simulator.wipeEeprom = checked
        }
    }

    SettingsGroupLayout {
        heading: qsTr("运行日志")
        headingDescription: qsTr("日志同时包含 ArduPlane SITL 和 QGC 内置目标模拟器输出，可用于判断启动失败原因。")

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 14
            clip: true

            TextArea {
                text: _simulator.logText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                font.family: ScreenTools.fixedFontFamily
                color: qgcPal.text
            }
        }

        QGCButton {
            text: qsTr("清空日志")
            enabled: _simulator.logText !== ""
            onClicked: _simulator.clearLog()
        }
    }
}
