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
import QGroundControl.Controls
import QGroundControl.Palette

/// Toolbar used for things like Polygon editing tools
Item {
    id:     root
    width:  Math.min(toolsRowLayout.width + (_margins * 2), availableWidth)
    height: toolsFlickable.y + toolsFlickable.height + _margins
    z:      QGroundControl.zOrderMapItems + 2

    property real availableWidth
    property Item backdropSource

    property real _radius:  ScreenTools.defaultFontPixelWidth / 2
    property real _margins: ScreenTools.defaultFontPixelWidth / 2

    Component.onCompleted: {
        // Move the child controls from consumer into the layout control
        var moveList = []
        var i
        for (i = 2; i < children.length; i++) {
            moveList.push(children[i])
        }
        for (i = 0; i < moveList.length; i++) {
            moveList[i].parent = toolsRowLayout
        }
        instructionComponent.createObject(toolsRowLayout)
    }

    GlassBackdrop {
        anchors.fill:       parent
        sourceItem:         root.backdropSource
        targetItem:         root
        cornerRadius:       root._radius
    }

    Rectangle {
        anchors.fill:   parent
        radius:         root._radius
        color:          "transparent"
        border.color:   Qt.rgba(0.82, 0.90, 0.95, 0.14)
        border.width:   1
    }

    QGCFlickable {
        id:                 toolsFlickable
        anchors.margins:    _margins
        anchors.top:        parent.top
        anchors.left:       parent.left
        anchors.right:      parent.right
        height:             toolsRowLayout.height
        clip:               true
        flickableDirection: Flickable.HorizontalFlick
        contentWidth:       toolsRowLayout.width

        RowLayout {
            id:                 toolsRowLayout
            spacing:            _margins
        }
    }

    Component {
        id: instructionComponent

        QGCLabel {
            id:             instructionLabel
            text:           _instructionText
        }
    }
}
