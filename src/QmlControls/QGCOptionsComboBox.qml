/****************************************************************************
 *
 * (c) 2009-2019 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 * @file
 *   @author Gus Grubba <gus@auterion.com>
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

ComboBox {
    id:         control
    padding:    ScreenTools.comboBoxPadding
    hoverEnabled: !ScreenTools.isMobile

    property string labelText:  qsTr("Options")

    signal itemClicked(int index)

    property var    _controlQGCPal: QGCPalette { colorGroupEnabled: enabled }
    property bool   _flashChecked
    property string _flashText
    property bool   _showFlash:     false

    Component.onCompleted: indicator.color = Qt.binding(function() { return _controlQGCPal.text })

    background: Rectangle {
        implicitWidth:                  ScreenTools.implicitComboBoxWidth
        implicitHeight:                 ScreenTools.implicitComboBoxHeight
        color:                          Qt.rgba(1, 1, 1, control.pressed ? 0.10 : (control.hovered ? 0.07 : 0.045))
        border.width:                   1
        border.color:                   Qt.rgba(0.82, 0.90, 0.95, control.hovered ? 0.26 : 0.18)
        radius:                         Math.round(ScreenTools.buttonBorderRadius * 1.25)
    }

    /*! Adding the Combobox list item to the theme.  */

    delegate: ItemDelegate {
        implicitHeight: modelData.visible ?
                            (Math.max(background ? background.implicitHeight : 0, Math.max(contentItem.implicitHeight, indicator ? indicator.implicitHeight : 0) + topPadding + bottomPadding)) :
                            0
        width:      control.width
        checkable:  true
        enabled:    modelData.enabled
        text:       modelData.text

        property var _checkedValue:     1
        property var _uncheckedValue:   0
        property var _itemQGCPal:       QGCPalette { colorGroupEnabled: enabled }
        property var _control:          control

        Binding on checked { value: modelData.fact ?
                                         (modelData.fact.typeIsBool ? (modelData.fact.value === false ? Qt.Unchecked : Qt.Checked) : (modelData.fact.value === 0 ? Qt.Unchecked : Qt.Checked)) :
                                         modelData.checked }

        contentItem: RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth

            Rectangle {
                height:         ScreenTools.defaultFontPixelHeight
                width:          height
                border.color:   checked ? _itemQGCPal.primaryButton : Qt.rgba(0.82, 0.90, 0.95, 0.24)
                border.width:   1
                color:          checked ? Qt.rgba(_itemQGCPal.primaryButton.r, _itemQGCPal.primaryButton.g, _itemQGCPal.primaryButton.b, 0.18)
                                        : Qt.rgba(1, 1, 1, 0.045)
                radius:         Math.round(width * 0.22)

                QGCColoredImage {
                    anchors.centerIn:   parent
                    width:              parent.width * 0.75
                    height:             width
                    source:             "/qmlimages/checkbox-check.svg"
                    color:              _itemQGCPal.buttonText
                    mipmap:             true
                    fillMode:           Image.PreserveAspectFit
                    sourceSize.height:  height
                    visible:            checked
                }
            }

            Text {
                text:   modelData.text
                color:  _itemQGCPal.buttonText
            }

        }

        background: Rectangle {
            color:          control.highlightedIndex === index ? Qt.rgba(1, 1, 1, 0.075) : "transparent"
            radius:         Math.round(ScreenTools.defaultFontPixelWidth * 0.45)
        }

        onClicked: {
            if (modelData.fact) {
                modelData.fact.value = (checked ? _checkedValue : _uncheckedValue)
            } else {
                itemClicked(index)
            }
            _control._flashChecked = checked
            _control._flashText = text
            _control._showFlash = true
            _control.popup.close()
        }
    }

    popup: Popup {
        x:              control.mirrored ? 0 : control.width - width
        y:              control.height
        width:          control.width
        height:         Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                                 control.Window.height - topMargin - bottomMargin)
        topMargin:      6
        bottomMargin:   6
        padding:        1

        contentItem: ListView {
            clip:                   true
            implicitHeight:         contentHeight
            model:                  control.popup.visible ? control.delegateModel : null
            currentIndex:           control.highlightedIndex
            highlightMoveDuration:  0

            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            color:          Qt.rgba(0.045, 0.048, 0.052, 0.94)
            radius:         Math.round(ScreenTools.defaultFontPixelWidth * 0.65)
            border.color:   Qt.rgba(0.82, 0.90, 0.95, 0.18)
            border.width:   1
        }
    }

    /*! This defines the label of the button.  */
    contentItem: Item {
        implicitWidth:                  _showFlash ? flash.implicitWidth : text.implicitWidth
        implicitHeight:                 _showFlash ? flash.implicitHeight : text.implicitHeight

        QGCLabel {
            id:                         text
            anchors.verticalCenter:     parent.verticalCenter
            text:                       labelText
            color:                      _controlQGCPal.text
            visible:                    !_showFlash
        }

        RowLayout {
            id:                     flash
            anchors.verticalCenter: parent.verticalCenter
            spacing:                ScreenTools.defaultFontPixelWidth
            visible:                _showFlash

            onVisibleChanged: {
                if (visible) {
                    flashTimer.restart()
                }
            }

            Timer {
                id:             flashTimer
                interval:       1500
                repeat:         false
                running:        false
                onTriggered:    _showFlash = false
            }

            Rectangle {
                height:         ScreenTools.defaultFontPixelHeight
                width:          height
                border.color:   _flashChecked ? _controlQGCPal.primaryButton : Qt.rgba(0.82, 0.90, 0.95, 0.24)
                border.width:   1
                color:          _flashChecked ? Qt.rgba(_controlQGCPal.primaryButton.r, _controlQGCPal.primaryButton.g, _controlQGCPal.primaryButton.b, 0.18)
                                              : Qt.rgba(1, 1, 1, 0.045)
                radius:         Math.round(width * 0.22)

                QGCColoredImage {
                    anchors.centerIn:   parent
                    width:              parent.width * 0.75
                    height:             width
                    source:             "/qmlimages/checkbox-check.svg"
                    color:              _controlQGCPal.text
                    mipmap:             true
                    fillMode:           Image.PreserveAspectFit
                    sourceSize.height:  height
                    visible:            _flashChecked
                }
            }

            Text {
                text:   _flashText
                color:  _controlQGCPal.buttonText
            }

        }
    }
}
