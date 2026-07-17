/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

PreFlightCheckButton {
    id: root

    property var vehicle
    property int testId:          -1
    property bool sending:        false
    property bool commandAccepted: false

    readonly property real _sendButtonWidth:  Math.max(ScreenTools.minTouchPixels * 1.25, ScreenTools.defaultFontPixelWidth * 7.5)
    readonly property real _sendButtonHeight: Math.max(ScreenTools.minTouchPixels * 0.78, ScreenTools.defaultFontPixelHeight * 1.7)
    readonly property color _commandAcceptedColor: "#4a90e2"

    _color: _telemetryState === _statePassed && _manualState === _statePassed ?
                _passedColor :
                (_telemetryState === _stateFailed ?
                     _failedColor :
                     (commandAccepted ? _commandAcceptedColor : _pendingColor))

    onVehicleChanged: {
        sending = false
        commandAccepted = false
    }

    Connections {
        target: root.vehicle
        ignoreUnknownSignals: true

        function onFlightCheckActuatorTestCommandFinished(finishedTestId, accepted) {
            if (finishedTestId === root.testId) {
                root.sending = false
                root.commandAccepted = accepted
            }
        }
    }

    Connections {
        target: root

        function onClicked() {
            root.commandAccepted = false
        }
    }

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: root.enabled
    }

    contentItem: RowLayout {
        spacing: ScreenTools.defaultFontPixelWidth

        ColumnLayout {
            Layout.fillWidth: true
            spacing:          Math.round(ScreenTools.defaultFontPixelHeight * 0.25)

            QGCLabel {
                Layout.fillWidth:    true
                text:                root.name
                font.bold:           true
                font.pointSize:      ScreenTools.defaultFontPointSize
                wrapMode:            Text.WordWrap
                horizontalAlignment: Text.AlignLeft
                color:               qgcPal.buttonText
            }

            QGCLabel {
                Layout.fillWidth:    true
                text:                root.manualText
                font.pointSize:      ScreenTools.smallFontPointSize
                wrapMode:            Text.WordWrap
                horizontalAlignment: Text.AlignLeft
                color:               qgcPal.buttonText
                opacity:             0.72
            }
        }

        QGCButton {
            id:                     sendButton
            glassStyle:             true
            Layout.alignment:       Qt.AlignRight | Qt.AlignVCenter
            Layout.rightMargin:     ScreenTools.defaultFontPixelWidth * 0.75
            Layout.minimumWidth:    root._sendButtonWidth
            Layout.preferredWidth:  root._sendButtonWidth
            Layout.maximumWidth:    root._sendButtonWidth
            Layout.minimumHeight:   root._sendButtonHeight
            Layout.preferredHeight: root._sendButtonHeight
            Layout.maximumHeight:   root._sendButtonHeight
            topPadding:             0
            bottomPadding:          0
            leftPadding:            ScreenTools.defaultFontPixelWidth
            rightPadding:           ScreenTools.defaultFontPixelWidth
            heightFactor:           0.18
            pointSize:              ScreenTools.smallFontPointSize
            backRadius:             Math.round(ScreenTools.defaultFontPixelWidth * 0.45)
            text:                   root.sending ? qsTr("Sending") : qsTr("Send")
            enabled:                root.enabled && !root.sending && !!root.vehicle
            onClicked: {
                root.commandAccepted = false
                root.sending = true
                if (!root.vehicle.startFlightCheckActuatorTest(root.testId)) {
                    root.sending = false
                }
            }

            contentItem: Item {
                QGCLabel {
                    anchors.centerIn: parent
                    text:             sendButton.text
                    font.pointSize:   ScreenTools.smallFontPointSize
                    color:            sendButton.pressed ? qgcPal.buttonHighlightText : qgcPal.buttonText
                }
            }
        }
    }

    function reset() {
        _manualState = manualText === "" ? _statePassed : _statePending
        if (telemetryFailure) {
            _telemetryState = allowTelemetryFailureOverride ? _statePending : _stateFailed
        } else {
            _telemetryState = _statePassed
        }
        commandAccepted = false
    }
}
