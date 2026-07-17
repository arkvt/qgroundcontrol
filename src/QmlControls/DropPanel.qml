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
import QtQuick.Layouts

import QGroundControl
import QGroundControl.ScreenTools
import QGroundControl.Palette

/// Drop panel that displays positioned next to the specified click position.
/// By default the panel drops to the right of the click position. If there isn't
/// enough room to the right then the panel will drop to the left.
Popup {
    id:             _root
    padding:        _innerMargin
    leftPadding:    _dropRight ? _innerMargin + _arrowPointWidth : _innerMargin
    rightPadding:   _dropRight ? _innerMargin : _innerMargin + _arrowPointWidth
    modal:          true
    focus:          true
    closePolicy:    Popup.CloseOnEscape | Popup.CloseOnPressOutside
    clip:           false
    dim:            false

    property var  sourceComponent                                               // Component to display within the popup
    property var  clickRect:        Qt.rect(0, 0, 0, 0)                         // Rectangle of clicked item - used to position drop down
    property var  dropViewPort:     Qt.rect(0, 0, parent.width, parent.height)  // Available viewport for dropdown
    property var  backdropSourceItem: null
    property bool backdropBlurEnabled: true

    property var  _qgcPal:              QGroundControl.globalPalette
    property real _innerMargin:         ScreenTools.defaultFontPixelWidth * 0.5 // Margin between content and rectanglular portion of background
    property real _arrowPointWidth:     ScreenTools.defaultFontPixelWidth * 2   // Distance from vertical side to point
    property real _arrowPointPositionY: height / 2
    property bool _dropRight:           true
    property int  _backdropSampleRevision: 0
    readonly property real _panelRadius: Math.round(ScreenTools.defaultFontPixelWidth * 0.78)
    readonly property color _panelTint:  Qt.rgba(0.045, 0.048, 0.052, 0.68)
    readonly property color _panelBorder: Qt.rgba(0.82, 0.90, 0.95, 0.14)

    onAboutToShow: {
        // Panel defaults to dropping to the right of click position
        let xPos = clickRect.x + clickRect.width

        // If there isn't room to the right then we switch to drop to the left
        if (xPos + _root.width > dropViewPort.x + dropViewPort.width) {
            _dropRight = false
            xPos = clickRect.x - _root.width
        }

        // Default position of panel is vertically centered on click position
        let yPos = clickRect.y + (clickRect.height / 2)
        yPos -= _root.height / 2

        // Make sure panel is within viewport
        let originalYPos = yPos
        yPos = Math.max(yPos, dropViewPort.y)
        yPos = Math.min(yPos, dropViewPort.y + dropViewPort.height - _root.height)

        _root.x = xPos
        _root.y = yPos

        // Adjust arrow position back to point at click position
        _arrowPointPositionY += originalYPos - yPos
        _backdropSampleRevision++
    }

    onOpened: _backdropSampleRevision++

    background: Item {
        implicitWidth:  contentItem.implicitWidth + _innerMargin * 2 + _arrowPointWidth
        implicitHeight: contentItem.implicitHeight + _innerMargin * 2

        Item {
            id:     panelSurface
            x:      _dropRight ? _arrowPointWidth : 0
            width:  parent.implicitWidth - _arrowPointWidth
            height: parent.implicitHeight
            clip:   true

            readonly property point backdropSamplePoint: {
                // mapToItem does not expose the Popup position as a binding dependency.
                // Read it explicitly so moving this dynamically-created Popup refreshes the sample.
                _root._backdropSampleRevision
                _root.x
                _root.y
                panelSurface.x
                panelSurface.y
                return _root.backdropSourceItem
                        ? panelSurface.mapToItem(_root.backdropSourceItem, 0, 0)
                        : Qt.point(0, 0)
            }

            GlassBackdrop {
                anchors.fill:        parent
                sourceItem:          _root.backdropSourceItem
                targetItem:          panelSurface
                sampleAtItemPosition: false
                sampleX:             panelSurface.backdropSamplePoint.x
                sampleY:             panelSurface.backdropSamplePoint.y
                backdropBlurEnabled: _root.backdropBlurEnabled && _root.visible && !!_root.backdropSourceItem
                cornerRadius:        _root._panelRadius
                sourcePadding:       48
            }

            Rectangle {
                anchors.fill: parent
                color:        "transparent"
                radius:       _root._panelRadius
                border.color: _root._panelBorder
                border.width: 1
            }
        }

        // Arrowhead
        Canvas {
            x:      _dropRight ? 0 : parent.width - _arrowPointWidth
            y:      _arrowPointPositionY - _arrowPointWidth
            width:  _arrowPointWidth
            height: _arrowPointWidth * 2
            
            onPaint: {
                var context = getContext("2d")
                context.reset()
                context.beginPath()
                context.moveTo(_dropRight ? 0 : _arrowPointWidth, _arrowPointWidth)
                context.lineTo(_dropRight ? _arrowPointWidth : 0, 0)
                context.lineTo(_dropRight ? _arrowPointWidth : 0, _arrowPointWidth * 2)
                context.closePath()
                context.fillStyle = _panelTint
                context.fill()
            }
        }
    }

    contentItem: SettingsGroupLayout {
        showBorder:   false
        showDividers: false

        Loader {
            sourceComponent: _root.sourceComponent
        }
    }
}
