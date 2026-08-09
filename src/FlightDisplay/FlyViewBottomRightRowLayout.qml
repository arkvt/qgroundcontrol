/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    id:             bottomStrip
    height:         Math.max(ScreenTools.minTouchPixels * 0.94, ScreenTools.defaultFontPixelHeight * 2.94)
    implicitWidth:  _contentPreferredWidth
    color:          "transparent"
    radius:         Math.round(ScreenTools.defaultFontPixelWidth * 0.78)
    border.color:   "transparent"
    border.width:   0
    clip:           false

    property var  _guidedController:   globals.guidedControllerFlyView
    property var  backdropSourceItem:  null
    property real _stripMargin:        ScreenTools.defaultFontPixelWidth * 0.38
    property real _rowSpacing:         ScreenTools.defaultFontPixelWidth * 0.38
    property real _operationSeparatorExtraGap: ScreenTools.defaultFontPixelWidth * 2
    property real _toolButtonWidth:    Math.max(ScreenTools.defaultFontPixelWidth * 7.35, ScreenTools.minTouchPixels * 1.02)
    property real _toolIconSize:       ScreenTools.defaultFontPixelHeight * 1.26
    property real _favoriteModeButtonWidth: Math.max(ScreenTools.defaultFontPixelWidth * 8.8, ScreenTools.minTouchPixels * 1.06)
    property real _favoriteModeGroupPadding: ScreenTools.defaultFontPixelWidth * 0.24
    property real _favoriteModeGroupSpacing: 1
    property int  _visibleToolActionCount: visibleToolActionCount()
    property int  _visibleFavoriteModeCount: flyActionList.activeVehicle && flyActionList.activeVehicle.flightModeSetAvailable ? flyActionList.favoriteModesForToolbar.length : 0
    property int  _visibleFavoriteModeGroupCount: _visibleFavoriteModeCount > 0 ? 1 : 0
    property int  _visibleLayoutItemCount: _visibleToolActionCount + _visibleFavoriteModeGroupCount
    property int  _operationSeparatorCount: operationSeparatorCount()
    property real _favoriteModeGroupWidth: _visibleFavoriteModeCount > 0 ?
                                                ((_favoriteModeGroupPadding * 2) +
                                                 (_visibleFavoriteModeCount * _favoriteModeButtonWidth) +
                                                 (Math.max(0, _visibleFavoriteModeCount - 1) * _favoriteModeGroupSpacing)) : 0
    property real _toolActionsWidth:   _visibleLayoutItemCount > 0 ?
                                            ((_visibleToolActionCount * _toolButtonWidth) +
                                             _favoriteModeGroupWidth +
                                             (Math.max(0, _visibleLayoutItemCount - 1) * _rowSpacing) +
                                             (_operationSeparatorCount * _operationSeparatorExtraGap)) : 0
    property real _contentPreferredWidth: (_stripMargin * 2) + _toolActionsWidth
    property real spacing:             0

    signal displayPreFlightChecklist

    QGCPalette { id: qgcPal }
    FlightModeDisplay { id: flightModeDisplay }

    GlassBackdrop {
        anchors.fill:       parent
        sourceItem:         bottomStrip.backdropSourceItem
        backdropBlurEnabled:true
        targetItem:         bottomStrip
        cornerRadius:       bottomStrip.radius
    }

    Rectangle {
        anchors.fill:   parent
        color:          "transparent"
        radius:         bottomStrip.radius
        border.color:   Qt.rgba(0.82, 0.90, 0.95, 0.14)
        border.width:   1
    }

    FlyViewToolStripActionList {
        id: flyActionList
        modePanelAvailableWidth: bottomStrip.parent ? bottomStrip.parent.width : ScreenTools.screenWidth
        onDisplayPreFlightChecklist: bottomStrip.displayPreFlightChecklist()
    }

    ToolStripDropPanel {
        id:                 dropPanel
        toolStrip:          bottomStrip
        allowOutsideParent: true
        keepOpenOnOutsideClick: flyActionList.modePanelPinned && _parentButton &&
                                _parentButton.toolStripAction === flyActionList.modeActionItem
        z:                  QGroundControl.zOrderWidgets + 1
    }

    function hideDropPanelAndUnpin() {
        if (dropPanel._parentButton && dropPanel._parentButton.toolStripAction === flyActionList.modeActionItem) {
            flyActionList.modePanelPinned = false
        }
        dropPanel.hide()
    }

    function clearOtherActionChecks(activeIndex) {
        var repeaters = [ leadingActionRepeater, trailingActionRepeater ]
        for (var repeaterIndex = 0; repeaterIndex < repeaters.length; repeaterIndex++) {
            var repeater = repeaters[repeaterIndex]
            for (var itemIndex = 0; itemIndex < repeater.count; itemIndex++) {
                var item = repeater.itemAt(itemIndex)
                if (item && item.actionIndex !== activeIndex) {
                    item.checked = false
                }
            }
        }
    }

    function modeActionIndex() {
        for (var i = 0; i < flyActionList.model.length; i++) {
            var action = flyActionList.model[i]
            if (action && typeof action.statusAction !== "undefined" && action.statusAction) {
                return i
            }
        }
        return -1
    }

    function actionsThroughMode() {
        var actions = []
        var splitIndex = modeActionIndex()
        var endIndex = splitIndex >= 0 ? splitIndex : flyActionList.model.length - 1
        for (var i = 0; i <= endIndex; i++) {
            actions.push(flyActionList.model[i])
        }
        return actions
    }

    function actionsAfterMode() {
        var actions = []
        var splitIndex = modeActionIndex()
        for (var i = splitIndex + 1; i < flyActionList.model.length; i++) {
            actions.push(flyActionList.model[i])
        }
        return actions
    }

    function actionVisible(action) {
        return action && action.visible && action.enabled
    }

    function actionIsNonOperation(action) {
        return action && typeof action.nonOperationAction !== "undefined" && action.nonOperationAction
    }

    function showOperationSeparatorBefore(actionIndex, action) {
        if (!actionVisible(action) || actionIsNonOperation(action)) {
            return false
        }

        for (var i = actionIndex - 1; i >= 0; i--) {
            var previousAction = flyActionList.model[i]
            if (actionVisible(previousAction)) {
                return actionIsNonOperation(previousAction)
            }
        }

        return false
    }

    function operationSeparatorCount() {
        var count = 0
        for (var i = 0; i < flyActionList.model.length; i++) {
            if (showOperationSeparatorBefore(i, flyActionList.model[i])) {
                count++
            }
        }
        return count
    }

    function dropPanelAnchorPoint(actionButton) {
        var anchorPoint = actionButton.mapToItem(bottomStrip, actionButton.width / 2, 0)
        if (actionButton.toolStripAction === flyActionList.modeActionItem) {
            anchorPoint.x = bottomStrip.width / 2
        }
        return anchorPoint
    }

    function visibleToolActionCount() {
        var count = 0
        for (var i = 0; i < flyActionList.model.length; i++) {
            if (actionVisible(flyActionList.model[i])) {
                count++
            }
        }
        return count
    }

    component ActionButton: Rectangle {
        id: actionButton

        property var  toolStripAction: null
        property bool checked:         toolStripAction ? toolStripAction.checked : false
        property bool checkable:       toolStripAction && (toolStripAction.dropPanelComponent || toolStripAction.checkable)
        property bool available:       toolStripAction && toolStripAction.enabled
        property int  actionIndex:     -1
        property string imageSource:   toolStripAction ? (toolStripAction.showAlternateIcon ? toolStripAction.alternateIconSource : toolStripAction.iconSource) : ""
        property string displayText:   toolStripAction ? String(toolStripAction.text).split("|")[0] : ""
        property string displayLabelText: flightModeDisplay.labelText(displayText)
        property string cornerBadgeText: toolStripAction && typeof toolStripAction.cornerBadgeText !== "undefined" ? toolStripAction.cornerBadgeText : ""
        property string inlineBadgeText:  cornerBadgeText !== "" ? "" : flightModeDisplay.badgeText(displayText)
        property bool showOperationSeparator: bottomStrip.showOperationSeparatorBefore(actionIndex, toolStripAction)
        property bool statusAction: toolStripAction && typeof toolStripAction.statusAction !== "undefined" && toolStripAction.statusAction

        visible:                bottomStrip.actionVisible(toolStripAction)
        Layout.preferredWidth:  visible ? _toolButtonWidth : 0
        Layout.minimumWidth:    visible ? _toolButtonWidth : 0
        Layout.maximumWidth:    visible ? _toolButtonWidth : 0
        Layout.fillHeight:      visible
        Layout.leftMargin:      visible && showOperationSeparator ? bottomStrip._operationSeparatorExtraGap : 0
        radius:                 Math.round(ScreenTools.defaultFontPixelWidth * 0.30)
        color:                  buttonMouse.pressed ? Qt.rgba(0.135, 0.140, 0.150, 0.44) :
                                    (statusAction ? ((available && buttonMouse.containsMouse) ? Qt.rgba(1, 1, 1, 0.040) : "transparent") :
                                     (checked ? Qt.rgba(0.82, 0.90, 0.95, 0.095) :
                                      ((available && buttonMouse.containsMouse) ? Qt.rgba(1, 1, 1, 0.060) : "transparent")))
        border.color:           statusAction ? (checked ? qgcPal.primaryButton : Qt.rgba(0.82, 0.90, 0.95, buttonMouse.containsMouse ? 0.30 : 0.20)) :
                                    (checked ? qgcPal.primaryButton :
                                     ((available && buttonMouse.containsMouse) ? Qt.rgba(0.82, 0.90, 0.95, 0.16) : "transparent"))
        border.width:           checked || statusAction || (available && buttonMouse.containsMouse) ? 1 : 0
        opacity:                available ? 1.0 : 0.48

        property color _contentColor: checked || statusAction ? qgcPal.text : qgcPal.buttonText
        property color _contentColorSecondary: qgcPal.colorGreen

        Rectangle {
            id:                 operationSeparator
            anchors.left:       parent.left
            anchors.leftMargin: -Math.round((bottomStrip._rowSpacing + bottomStrip._operationSeparatorExtraGap + width) / 2)
            anchors.verticalCenter: parent.verticalCenter
            width:              1
            height:             parent.height * 0.58
            radius:             width / 2
            color:              Qt.rgba(0.82, 0.90, 0.95, 0.22)
            visible:            actionButton.showOperationSeparator
        }

        onCheckedChanged: {
            if (toolStripAction && toolStripAction.checked !== checked) {
                toolStripAction.checked = checked
            }
        }

        ColumnLayout {
            anchors.centerIn:   parent
            width:              parent.width - ScreenTools.defaultFontPixelWidth * 0.48
            spacing:            0

            Image {
                Layout.alignment:   Qt.AlignHCenter
                source:             actionButton.imageSource
                visible:            source !== "" && actionButton.toolStripAction && actionButton.toolStripAction.fullColorIcon
                width:              bottomStrip._toolIconSize
                height:             width
                sourceSize.width:   width
                sourceSize.height:  height
                fillMode:           Image.PreserveAspectFit
                smooth:             true
                mipmap:             true
            }

            QGCColoredImage {
                id:                 actionIcon
                Layout.alignment:   Qt.AlignHCenter
                source:             actionButton.imageSource
                visible:            source !== "" && (!actionButton.toolStripAction || !actionButton.toolStripAction.fullColorIcon)
                color:              actionButton._contentColor
                width:              bottomStrip._toolIconSize
                height:             width
                sourceSize.width:   width
                sourceSize.height:  height
                fillMode:           Image.PreserveAspectFit

                QGCColoredImage {
                    anchors.centerIn:   parent
                    source:             actionButton.toolStripAction ? actionButton.toolStripAction.alternateIconSource : ""
                    visible:            source !== "" && actionButton.toolStripAction && actionButton.toolStripAction.biColorIcon
                    color:              actionButton._contentColorSecondary
                    width:              parent.width
                    height:             parent.height
                    sourceSize.width:   width
                    sourceSize.height:  height
                    fillMode:           Image.PreserveAspectFit
                }
            }

            Item {
                id:                         actionTextHost
                Layout.alignment:           Qt.AlignHCenter
                Layout.fillWidth:           true
                Layout.preferredHeight:     Math.max(actionTextLabel.implicitHeight,
                                                     actionInlineBadge.visible ? actionInlineBadge.height : 0)

                RowLayout {
                    id:                     actionTextRow
                    anchors.centerIn:       parent
                    spacing:                actionInlineBadge.visible ? ScreenTools.defaultFontPixelWidth * 0.22 : 0

                    QGCLabel {
                        id:                     actionTextLabel
                        Layout.preferredWidth:  Math.max(0, Math.min(implicitWidth,
                                                                     actionTextHost.width -
                                                                     (actionInlineBadge.visible ? actionInlineBadge.width + actionTextRow.spacing : 0)))
                        Layout.maximumWidth:    Layout.preferredWidth
                        text:                   actionButton.displayLabelText
                        color:                  actionButton._contentColor
                        font.bold:              actionButton.checked || actionButton.imageSource === ""
                        font.pointSize:         ScreenTools.controlFontPointSize
                        horizontalAlignment:    Text.AlignHCenter
                        verticalAlignment:      Text.AlignVCenter
                        fontSizeMode:           Text.HorizontalFit
                        minimumPointSize:       ScreenTools.captionFontPointSize
                        elide:                  Text.ElideNone
                        maximumLineCount:       1
                    }

                    Rectangle {
                        id:                 actionInlineBadge
                        Layout.alignment:   Qt.AlignVCenter
                        Layout.preferredWidth: actionInlineBadgeLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 0.42
                        Layout.preferredHeight: Math.max(ScreenTools.defaultFontPixelHeight * 0.66,
                                                         actionInlineBadgeLabel.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.04)
                        radius:             Math.round(height * 0.28)
                        color:              Qt.rgba(0.82, 0.88, 0.94, actionButton.checked ? 0.16 : 0.10)
                        border.color:       Qt.rgba(0.82, 0.88, 0.94, actionButton.checked ? 0.28 : 0.18)
                        border.width:       1
                        visible:            actionButton.inlineBadgeText !== ""

                        QGCLabel {
                            id:                     actionInlineBadgeLabel
                            anchors.centerIn:       parent
                            text:                   actionButton.inlineBadgeText
                            color:                  qgcPal.buttonText
                            font.bold:              true
                            font.pointSize:         Math.max(7, ScreenTools.captionFontPointSize - 1)
                            horizontalAlignment:    Text.AlignHCenter
                            verticalAlignment:      Text.AlignVCenter
                            maximumLineCount:       1
                        }
                    }
                }
            }
        }

        Rectangle {
            id:                 actionCornerBadge
            anchors.top:        parent.top
            anchors.right:      parent.right
            anchors.topMargin:  ScreenTools.defaultFontPixelHeight * 0.20
            anchors.rightMargin:ScreenTools.defaultFontPixelWidth * 0.20
            width:              actionCornerBadgeLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 0.42
            height:             Math.max(ScreenTools.defaultFontPixelHeight * 0.66,
                                         actionCornerBadgeLabel.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.04)
            radius:             Math.round(height * 0.28)
            color:              Qt.rgba(0.82, 0.88, 0.94, actionButton.checked ? 0.16 : 0.10)
            border.color:       Qt.rgba(0.82, 0.88, 0.94, actionButton.checked ? 0.28 : 0.18)
            border.width:       1
            visible:            actionButton.cornerBadgeText !== ""
            z:                  3

            QGCLabel {
                id:                     actionCornerBadgeLabel
                anchors.centerIn:       parent
                text:                   actionButton.cornerBadgeText
                color:                  qgcPal.buttonText
                font.bold:              true
                font.pointSize:         Math.max(7, ScreenTools.captionFontPointSize - 1)
                horizontalAlignment:    Text.AlignHCenter
                verticalAlignment:      Text.AlignVCenter
                maximumLineCount:       1
            }
        }

        QGCMouseArea {
            id:             buttonMouse
            anchors.fill:   parent
            enabled:        actionButton.available
            hoverEnabled:   !ScreenTools.isMobile
            onClicked: {
                if (!actionButton.toolStripAction) {
                    return
                }
                if (mainWindow.allowViewSwitch()) {
                    bottomStrip.hideDropPanelAndUnpin()
                    if (!actionButton.toolStripAction.dropPanelComponent) {
                        actionButton.toolStripAction.triggered(actionButton)
                    } else {
                        actionButton.checked = true
                        bottomStrip.clearOtherActionChecks(actionButton.actionIndex)
                        var panelEdgeTopPoint = bottomStrip.dropPanelAnchorPoint(actionButton)
                        dropPanel.show(panelEdgeTopPoint, actionButton.toolStripAction.dropPanelComponent, actionButton)
                    }
                } else if (actionButton.checkable) {
                    actionButton.checked = !actionButton.checked
                }
            }
        }
    }

    component FavoriteModeGroup: Rectangle {
        id: favoriteModeGroup

        property var modes: []

        visible:                modes && modes.length > 0
        Layout.preferredWidth:  visible ? bottomStrip._favoriteModeGroupWidth : 0
        Layout.minimumWidth:    Layout.preferredWidth
        Layout.maximumWidth:    Layout.preferredWidth
        Layout.fillHeight:      visible
        radius:                 Math.round(ScreenTools.defaultFontPixelWidth * 0.36)
        color:                  Qt.rgba(0.035, 0.040, 0.046, 0.46)
        border.color:           Qt.rgba(0.82, 0.90, 0.95, 0.20)
        border.width:           1

        RowLayout {
            id:                 favoriteModeRow
            anchors.fill:       parent
            anchors.margins:    bottomStrip._favoriteModeGroupPadding
            spacing:            bottomStrip._favoriteModeGroupSpacing

            Repeater {
                model: favoriteModeGroup.modes

                Rectangle {
                    id: favoriteModeButton

                    property var vehicle: flyActionList.activeVehicle
                    property string modeName: modelData
                    property string displayModeText: flightModeDisplay.modeText(vehicle, modeName, modeName)
                    property bool activeMode: vehicle && vehicle.flightMode === modeName

                    Layout.preferredWidth:  bottomStrip._favoriteModeButtonWidth
                    Layout.minimumWidth:    Layout.preferredWidth
                    Layout.maximumWidth:    Layout.preferredWidth
                    Layout.fillHeight:      true
                    radius:                 Math.round(ScreenTools.defaultFontPixelWidth * 0.24)
                    color:                  favoriteModeMouse.pressed ? Qt.rgba(0.16, 0.20, 0.24, 0.72) :
                                                (activeMode ? Qt.rgba(0.10, 0.56, 0.83, 0.16) :
                                                 (favoriteModeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.025)))
                    border.color:           activeMode ? qgcPal.primaryButton :
                                                (favoriteModeMouse.containsMouse ? Qt.rgba(0.82, 0.90, 0.95, 0.20) : "transparent")
                    border.width:           activeMode || favoriteModeMouse.containsMouse ? 1 : 0

                    Item {
                        id:                 favoriteModeTextHost
                        anchors.fill:       parent
                        anchors.margins:    ScreenTools.defaultFontPixelWidth * 0.28

                        RowLayout {
                            anchors.centerIn: parent

                            QGCLabel {
                                id:                     favoriteModeLabel
                                Layout.preferredWidth:  Math.max(0, Math.min(implicitWidth, favoriteModeTextHost.width))
                                Layout.maximumWidth:    Layout.preferredWidth
                                text:                   flightModeDisplay.labelText(favoriteModeButton.displayModeText)
                                color:                  favoriteModeButton.activeMode ? qgcPal.text : qgcPal.buttonText
                                font.bold:              true
                                font.pointSize:         ScreenTools.controlFontPointSize
                                horizontalAlignment:    Text.AlignHCenter
                                verticalAlignment:      Text.AlignVCenter
                                fontSizeMode:           Text.HorizontalFit
                                minimumPointSize:       ScreenTools.captionFontPointSize
                                elide:                  Text.ElideNone
                                maximumLineCount:       1
                            }
                        }
                    }

                    QGCMouseArea {
                        id:             favoriteModeMouse
                        anchors.fill:   parent
                        hoverEnabled:   !ScreenTools.isMobile
                        enabled:        favoriteModeButton.vehicle && favoriteModeButton.vehicle.flightModeSetAvailable
                        onClicked: {
                            if (mainWindow.allowViewSwitch()) {
                                flyActionList.setFavoriteFlightMode(favoriteModeButton.modeName)
                            }
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.fill:       parent
        anchors.margins:    _stripMargin
        spacing:            _rowSpacing

        QGCFlickable {
            id:                     actionFlick
            property real _overflowEpsilon: ScreenTools.defaultFontPixelWidth * 0.25
            property bool _rawHorizontalOverflow: contentWidth > width + _overflowEpsilon
            property bool _stableHorizontalOverflow: false

            Layout.fillWidth:       true
            Layout.fillHeight:      true
            Layout.preferredWidth:  bottomStrip._toolActionsWidth
            Layout.minimumWidth:    0
            contentWidth:           bottomStrip._toolActionsWidth
            contentHeight:          height
            flickableDirection:     _stableHorizontalOverflow ? Flickable.HorizontalFlick : Flickable.VerticalFlick
            boundsBehavior:         Flickable.StopAtBounds
            interactive:            _stableHorizontalOverflow
            clip:                   true
            indicatorColor:         qgcPal.buttonText

            function _refreshHorizontalOverflow() {
                if (_rawHorizontalOverflow) {
                    overflowSettleTimer.restart()
                } else {
                    overflowSettleTimer.stop()
                    _stableHorizontalOverflow = false
                    contentX = 0
                }
            }

            on_RawHorizontalOverflowChanged: _refreshHorizontalOverflow()
            Component.onCompleted:           _refreshHorizontalOverflow()

            Timer {
                id:         overflowSettleTimer
                interval:   120
                repeat:     false
                onTriggered: actionFlick._stableHorizontalOverflow = actionFlick._rawHorizontalOverflow
            }

            on_StableHorizontalOverflowChanged: {
                if (!_stableHorizontalOverflow) {
                    contentX = 0
                }
            }

            RowLayout {
                id:                 actionRow
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                spacing:            _rowSpacing

                Repeater {
                    id:     leadingActionRepeater
                    model:  bottomStrip.actionsThroughMode()

                    ActionButton {
                        toolStripAction: modelData
                        actionIndex:     index
                    }
                }

                FavoriteModeGroup {
                    id: favoriteModeGroupItem
                    modes: flyActionList.activeVehicle && flyActionList.activeVehicle.flightModeSetAvailable ?
                                flyActionList.favoriteModesForToolbar : []
                }

                Repeater {
                    id:     trailingActionRepeater
                    model:  bottomStrip.actionsAfterMode()

                    ActionButton {
                        toolStripAction: modelData
                        actionIndex:     bottomStrip.modeActionIndex() + 1 + index
                    }
                }
            }
        }

    }
}
