import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl.Palette
import QGroundControl.ScreenTools

/// Standard push button control:
///     If there is both an icon and text the icon will be to the left of the text
///     If icon only, icon will be centered
Button {
    id:             control
    hoverEnabled:   !ScreenTools.isMobile
    topPadding:     _verticalPadding
    bottomPadding:  _verticalPadding
    leftPadding:    _horizontalPadding
    rightPadding:   _horizontalPadding
    focusPolicy:    Qt.ClickFocus
    font.family:    ScreenTools.normalFontFamily
    text:           ""

    property bool   primary:        false                               ///< primary button for a group of buttons
    property bool   showBorder:     qgcPal.globalTheme === QGCPalette.Light
    property bool   glassStyle:     false                               ///< translucent surface for map/flight overlays
    property real   backRadius:     Math.round(ScreenTools.defaultFontPixelWidth * 0.65)
    property real   heightFactor:   0.55
    property string iconSource:     ""
    property real   fontWeight:     Font.Normal // default for qml Text
    property real   pointSize:      ScreenTools.controlFontPointSize

    property alias wrapMode:            text.wrapMode
    property alias horizontalAlignment: text.horizontalAlignment
    property alias verticalAlignment:   text.verticalAlignment
    property alias backgroundColor:     backRect.color
    property alias textColor:           text.color

    property bool   _showHighlight:     enabled && (pressed || checked)
    readonly property bool _lightTheme: qgcPal.globalTheme === QGCPalette.Light

    property int _horizontalPadding:    ScreenTools.defaultFontPixelWidth * 2
    property int _verticalPadding:      Math.round(ScreenTools.defaultFontPixelHeight * heightFactor)

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    background: Rectangle {
        id:             backRect
        radius:         backRadius
        implicitWidth:  ScreenTools.implicitButtonWidth
        implicitHeight: Math.round(ScreenTools.implicitButtonHeight * 1.1)
        border.width:   glassStyle || showBorder || control.enabled ? 1 : 0
        border.color:   primary || (glassStyle && checked) ? qgcPal.primaryButton :
                            (glassStyle
                                ? (_lightTheme
                                    ? Qt.rgba(0.10, 0.16, 0.20, control.hovered ? 0.30 : 0.20)
                                    : Qt.rgba(0.82, 0.90, 0.95, control.hovered ? 0.28 : 0.17))
                                : qgcPal.buttonBorder)
        color:          primary ? qgcPal.primaryButton :
                            (glassStyle
                                ? (checked
                                    ? Qt.rgba(qgcPal.primaryButton.r, qgcPal.primaryButton.g, qgcPal.primaryButton.b, 0.15)
                                    : (_lightTheme
                                    ? Qt.rgba(0.96, 0.98, 1.00, control.pressed ? 0.88 : (control.hovered ? 0.78 : 0.66))
                                    : Qt.rgba(1, 1, 1, control.pressed ? 0.135 : (control.hovered ? 0.105 : 0.065))))
                                : qgcPal.button)
        opacity:        control.enabled ? 1.0 : 0.52

        Rectangle {
            anchors.fill:   parent
            color:          glassStyle ? (checked ? qgcPal.primaryButton : "white") : qgcPal.buttonHighlight
            opacity:        glassStyle
                                ? (control.pressed ? 0.08 : (checked ? 0.035 : 0))
                                : (_showHighlight ? 0.92 : control.enabled && control.hovered ? 0.18 : 0)
            radius:         parent.radius
        }

        Rectangle {
            anchors.left:        parent.left
            anchors.right:       parent.right
            anchors.top:         parent.top
            anchors.leftMargin:  Math.max(1, parent.radius * 0.45)
            anchors.rightMargin: Math.max(1, parent.radius * 0.45)
            height:              1
            color:               _lightTheme ? Qt.rgba(1, 1, 1, 0.62) : Qt.rgba(1, 1, 1, 0.16)
            visible:             glassStyle
        }
    }

    contentItem: RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth * 0.65

            QGCColoredImage {
                id:                     icon
                Layout.alignment:       Qt.AlignHCenter | Qt.AlignVCenter
                source:                 control.iconSource
                height:                 text.height
                width:                  height
                color:                  text.color
                fillMode:               Image.PreserveAspectFit
                sourceSize.height:      height
                visible:                control.iconSource !== ""
            }

            QGCLabel {
                id:                     text
                Layout.alignment:       Qt.AlignHCenter | Qt.AlignVCenter
                text:                   control.text
                font.pointSize:         control.pointSize
                font.family:            control.font.family
                font.weight:            fontWeight
                color:                  _showHighlight ? qgcPal.buttonHighlightText : (primary ? qgcPal.primaryButtonText : qgcPal.buttonText)
                verticalAlignment:      Text.AlignVCenter
                visible:                control.text !== "" 
            }
    }
}
