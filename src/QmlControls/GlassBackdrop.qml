/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Effects

Item {
    id: glassBackdrop

    property Item  sourceItem:            null
    property Item  targetItem:            parent
    property bool  sampleAtItemPosition:  true
    property real  sampleX:               0
    property real  sampleY:               0
    property real  sourcePadding:         0
    // Canonical workspace glass material. Callers configure only geometry and sampling;
    // keeping the optical values here prevents individual overlays from drifting apart.
    property real  sourceScale:           0.46
    property real  blurAmount:            0.94
    property real  blurMax:               42
    property real  sourceBrightness:     -0.01
    // Retained source saturation: 1.0 keeps the original color, 0.0 is grayscale.
    // MultiEffect.saturation is an adjustment value, so convert this ratio below.
    property real  sourceSaturation:      0.62
    property color tintColor:             Qt.rgba(0.045, 0.048, 0.052, 0.68)
    property color sheenColor:            "transparent"
    property bool  backdropBlurEnabled:   true
    property real  cornerRadius:          0

    readonly property real _effectiveSourcePadding: Math.max(0, sourcePadding)

    clip: true

    function sourceSampleRect() {
        if (!sourceItem || !targetItem) {
            return Qt.rect(0, 0, Math.max(1, width), Math.max(1, height))
        }

        var sourcePoint = sampleAtItemPosition ? targetItem.mapToItem(sourceItem, 0, 0) : Qt.point(sampleX, sampleY)
        return Qt.rect(sourcePoint.x - _effectiveSourcePadding,
                       sourcePoint.y - _effectiveSourcePadding,
                       Math.max(1, width + (_effectiveSourcePadding * 2)),
                       Math.max(1, height + (_effectiveSourcePadding * 2)))
    }

    Item {
        id: emptySource
        width:  1
        height: 1
        visible: false
    }

    Item {
        id:     blurLayer
        x:      -glassBackdrop._effectiveSourcePadding
        y:      -glassBackdrop._effectiveSourcePadding
        width:  glassBackdrop.width + (glassBackdrop._effectiveSourcePadding * 2)
        height: glassBackdrop.height + (glassBackdrop._effectiveSourcePadding * 2)

        ShaderEffectSource {
            id:             backdropTexture
            anchors.fill:   parent
            visible:        false
            live:           glassBackdrop.backdropBlurEnabled && !!glassBackdrop.sourceItem && glassBackdrop.visible
            recursive:      false
            hideSource:     false
            sourceItem:     glassBackdrop.sourceItem ? glassBackdrop.sourceItem : emptySource
            sourceRect:     glassBackdrop.sourceSampleRect()
            textureSize:    Qt.size(Math.max(1, Math.round(blurLayer.width * glassBackdrop.sourceScale)),
                                    Math.max(1, Math.round(blurLayer.height * glassBackdrop.sourceScale)))
        }

        MultiEffect {
            anchors.fill:   parent
            visible:        glassBackdrop.backdropBlurEnabled && !!glassBackdrop.sourceItem && glassBackdrop.blurAmount > 0
            source:         backdropTexture
            autoPaddingEnabled: false
            blurEnabled:    glassBackdrop.backdropBlurEnabled && glassBackdrop.blurAmount > 0
            blurMax:        glassBackdrop.blurMax
            blur:           glassBackdrop.blurAmount
            brightness:     glassBackdrop.sourceBrightness
            saturation:     Math.max(-1.0, glassBackdrop.sourceSaturation - 1.0)
            maskEnabled:    glassBackdrop.cornerRadius > 0
            maskSource:     roundedMask
        }
    }

    Rectangle {
        anchors.fill: parent
        color:        glassBackdrop.tintColor
        radius:       glassBackdrop.cornerRadius
    }

    Rectangle {
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.top:    parent.top
        height:         Math.max(1, parent.height * 0.18)
        color:          glassBackdrop.sheenColor
        radius:         glassBackdrop.cornerRadius
    }

    Item {
        id:             roundedMask
        width:          blurLayer.width
        height:         blurLayer.height
        visible:        false
        layer.enabled:  true

        Rectangle {
            x:              glassBackdrop._effectiveSourcePadding
            y:              glassBackdrop._effectiveSourcePadding
            width:          glassBackdrop.width
            height:         glassBackdrop.height
            radius:         glassBackdrop.cornerRadius
            color:          "black"
        }
    }
}
