/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.ScreenTools
import QGroundControl.Controls
import QGroundControl.Palette

Rectangle {
    id:                 _root
    anchors.margins:    -ScreenTools.defaultFontPixelHeight
    height:             warningsCol.height
    width:              warningsCol.width
    color:              "transparent"
    border.color:       "transparent"
    border.width:       0
    radius:             Math.round(ScreenTools.defaultFontPixelWidth * 0.78)
    clip:               true
    visible:            _noGPSLockVisible || _prearmErrorVisible

    property var  backdropSourceItem
    property var  _activeVehicle:       QGroundControl.multiVehicleManager.activeVehicle
    property bool _noGPSLockVisible:    _activeVehicle && _activeVehicle.requiresGpsFix && !_activeVehicle.coordinate.isValid
    property bool _prearmErrorVisible:  _activeVehicle && !_activeVehicle.armed && _activeVehicle.prearmError && !_activeVehicle.healthAndArmingCheckReport.supported

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    GlassBackdrop {
        anchors.fill:        parent
        sourceItem:          _root.backdropSourceItem
        targetItem:          _root
        backdropBlurEnabled: _root.visible && !!_root.backdropSourceItem
        cornerRadius:        _root.radius
    }

    Rectangle {
        anchors.fill: parent
        color:        "transparent"
        radius:       _root.radius
        border.color: Qt.rgba(0.82, 0.90, 0.95, 0.14)
        border.width: 1
    }

    Column {
        id:         warningsCol
        spacing:    ScreenTools.defaultFontPixelHeight

        QGCLabel {
            anchors.horizontalCenter:   parent.horizontalCenter
            visible:                    _noGPSLockVisible
            color:                      qgcPal.text
            font.pointSize:             ScreenTools.sectionFontPointSize
            text:                       qsTr("No GPS Lock for Vehicle")
        }

        QGCLabel {
            anchors.horizontalCenter:   parent.horizontalCenter
            visible:                    _prearmErrorVisible
            color:                      qgcPal.text
            font.pointSize:             ScreenTools.sectionFontPointSize
            text:                       _activeVehicle ? _activeVehicle.prearmError : ""
        }

        QGCLabel {
            anchors.horizontalCenter:   parent.horizontalCenter
            visible:                    _prearmErrorVisible
            width:                      ScreenTools.defaultFontPixelWidth * 50
            horizontalAlignment:        Text.AlignHCenter
            wrapMode:                   Text.WordWrap
            color:                      qgcPal.text
            font.pointSize:             ScreenTools.sectionFontPointSize
            text:                       qsTr("The vehicle has failed a pre-arm check. In order to arm the vehicle, resolve the failure.")
        }
    }
}
