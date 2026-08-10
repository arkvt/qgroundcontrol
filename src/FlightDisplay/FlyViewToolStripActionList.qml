/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQml.Models
import QtQuick.Layouts
import QtQuick.Controls
import QtCore

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.Palette
import QGroundControl.ScreenTools

ToolStripActionList {
    id: _root

    signal displayPreFlightChecklist

    property var flightModeDisplay: FlightModeDisplay { }
    property var activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property bool modePanelPinned: false
    property var modeActionItem: modeAction
    property var favoriteModeNames: []
    property var favoriteModes: []
    property var favoriteModesForToolbar: favoriteModes
    property real modePanelAvailableWidth: ScreenTools.screenWidth
    property string _favoriteContextKey: _vehicleFavoriteContextKey(activeVehicle)

    property var _favoriteSettings: Settings {
        category: "FlyViewFavoriteFlightModes"
        property string modesByVehicleType: "{}"
    }

    property var _favoriteVehicleConnections: Connections {
        target: _root.activeVehicle
        function onFlightModesChanged() { _root._syncAvailableFavoriteModes() }
    }

    on_FavoriteContextKeyChanged: _loadFavoriteModes()
    Component.onCompleted: _loadFavoriteModes()

    function _vehicleFavoriteContextKey(vehicle) {
        if (!vehicle) {
            return ""
        }

        var firmwareKey = vehicle.apmFirmware ? "apm" : (vehicle.px4Firmware ? "px4" : "other")
        return firmwareKey + ":" + vehicle.vehicleClassInternalName()
    }

    function _favoriteModeMap() {
        try {
            var favoriteMap = JSON.parse(_favoriteSettings.modesByVehicleType)
            return favoriteMap && typeof favoriteMap === "object" ? favoriteMap : {}
        } catch (error) {
            console.warn("Unable to load favorite flight modes:", error)
            return {}
        }
    }

    function _loadFavoriteModes() {
        if (_favoriteContextKey === "") {
            favoriteModeNames = []
            favoriteModes = []
            return
        }

        var favoriteMap = _favoriteModeMap()
        var storedModes = favoriteMap[_favoriteContextKey]
        favoriteModeNames = storedModes && storedModes.length !== undefined ? storedModes.slice() : []
        _syncAvailableFavoriteModes()
    }

    function _saveFavoriteModes() {
        if (_favoriteContextKey === "") {
            return
        }

        var favoriteMap = _favoriteModeMap()
        favoriteMap[_favoriteContextKey] = favoriteModeNames
        _favoriteSettings.modesByVehicleType = JSON.stringify(favoriteMap)
    }

    function _syncAvailableFavoriteModes() {
        var availableFavorites = []
        var vehicleModes = activeVehicle && activeVehicle.flightModes ? activeVehicle.flightModes : []

        for (var favoriteIndex = 0; favoriteIndex < favoriteModeNames.length; favoriteIndex++) {
            for (var modeIndex = 0; modeIndex < vehicleModes.length; modeIndex++) {
                if (favoriteModeNames[favoriteIndex] === vehicleModes[modeIndex]) {
                    availableFavorites.push(vehicleModes[modeIndex])
                    break
                }
            }
        }

        favoriteModes = availableFavorites
    }

    function isFavoriteMode(mode) {
        return favoriteModeNames.indexOf(mode) !== -1
    }

    function toggleFavoriteMode(mode) {
        if (!mode || _favoriteContextKey === "") {
            return
        }

        var updatedModes = favoriteModeNames.slice()
        var favoriteIndex = updatedModes.indexOf(mode)
        if (favoriteIndex === -1) {
            updatedModes.push(mode)
        } else {
            updatedModes.splice(favoriteIndex, 1)
        }

        favoriteModeNames = updatedModes
        _syncAvailableFavoriteModes()
        _saveFavoriteModes()
    }

    function setFavoriteFlightMode(mode) {
        if (activeVehicle && activeVehicle.flightModeSetAvailable && mode) {
            activeVehicle.flightMode = mode
        }
    }

    model: [
        ToolStripAction {
            property bool _is3DViewOpen:            viewer3DWindow.isOpen
            property bool   _viewer3DEnabled:       QGroundControl.settingsManager.viewer3DSettings.enabled.rawValue

            id: view3DIcon
            property bool nonOperationAction: true
            visible: _viewer3DEnabled
            text:           qsTr("3D View")
            iconSource:     "/res/flyview-3d.svg"
            onTriggered:{
                if(_is3DViewOpen === false){
                    viewer3DWindow.open()
                }else{
                    viewer3DWindow.close()
                }
            }

            on_Is3DViewOpenChanged: {
                if(_is3DViewOpen === true){
                    view3DIcon.iconSource =     "/res/flyview-fly.svg"
                    text=           qsTr("Fly")
                }else{
                    iconSource =     "/res/flyview-3d.svg"
                    text =           qsTr("3D View")
                }
            }
        },
        ToolStripAction {
            property bool nonOperationAction: true

            text:       qsTr("Plan")
            iconSource: "/res/flyview-plan.svg"

            onTriggered: {
                mainWindow.showPlanView()
            }
        },
        PreFlightCheckListShowAction {
            property bool nonOperationAction: true
            onTriggered: displayPreFlightChecklist()
        },
        ToolStripAction {
            id: modeAction

            property var _activeVehicle: _root.activeVehicle
            property string _currentModeDisplayText: _activeVehicle ? flightModeDisplay.shortModeText(_activeVehicle, _activeVehicle.flightMode, qsTr("Mode")) : qsTr("Mode")
            property string cornerBadgeText: flightModeDisplay.badgeText(_currentModeDisplayText)
            property bool statusAction: true

            text:       flightModeDisplay.labelText(_currentModeDisplayText)
            iconSource: "/res/flyview-mode.svg"
            visible:    _activeVehicle && _activeVehicle.flightModeSetAvailable
            enabled:    visible

            dropPanelComponent: Component {
                Item {
                    id:     flightModePanel
                    width:  _gridWidth + ((_pinButtonWidth + _pinGap) * 2)
                    height: _visibleGridHeight
                    clip:   false

                    property bool frameless: true
                    property var _activeVehicle: _root.activeVehicle
                    property real _rowHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.10,
                                                       ScreenTools.minTouchPixels * 0.94)
                    property real _rowSpacing: ScreenTools.defaultFontPixelHeight * 0.30
                    property real _columnSpacing: ScreenTools.defaultFontPixelWidth * 0.42
                    property real _modeButtonWidth: ScreenTools.isMobile ?
                                                        Math.max(ScreenTools.defaultFontPixelWidth * 9.6,
                                                                 ScreenTools.minTouchPixels * 1.48) :
                                                        Math.max(ScreenTools.defaultFontPixelWidth * 10.4,
                                                                 ScreenTools.minTouchPixels * 1.60)
                    property real _pinButtonWidth: Math.max(ScreenTools.defaultFontPixelHeight * 1.45,
                                                             ScreenTools.minTouchPixels * 0.68)
                    property real _pinGap: _columnSpacing
                    property var  _modeList: flightModeDisplay.sortedModes(_activeVehicle)
                    property int  _modeCount: _modeList.length
                    property int  _preferredMaxColumns: ScreenTools.isMobile ? 2 : 15
                    property real _gridAvailableWidth: Math.max(_modeButtonWidth,
                                                                 _root.modePanelAvailableWidth -
                                                                 (ScreenTools.defaultFontPixelWidth * 2) -
                                                                 ((_pinButtonWidth + _pinGap) * 2))
                    property int  _widthLimitedColumns: Math.max(1, Math.floor((_gridAvailableWidth + _columnSpacing) /
                                                                               (_modeButtonWidth + _columnSpacing)))
                    property int  _maxColumns: Math.max(1, Math.min(_preferredMaxColumns, _widthLimitedColumns))
                    property int  _modeRowCount: Math.max(1, Math.ceil(_modeCount / _maxColumns))
                    property int  _gridColumns: Math.max(1, Math.ceil(_modeCount / _modeRowCount))
                    property int  _rowBaseCount: Math.floor(_modeCount / _modeRowCount)
                    property int  _rowExtraCount: _modeCount % _modeRowCount
                    property real _gridWidth: (_modeButtonWidth * _gridColumns) +
                                               (_columnSpacing * Math.max(0, _gridColumns - 1))
                    property real _visibleGridHeight: (_rowHeight * _modeRowCount) +
                                                       (_rowSpacing * Math.max(0, _modeRowCount - 1))

                    function _rowItemCount(rowIndex) {
                        return _rowBaseCount + (rowIndex >= _modeRowCount - _rowExtraCount ? 1 : 0)
                    }

                    function _rowStartIndex(rowIndex) {
                        return (rowIndex * _rowBaseCount) +
                               Math.max(0, rowIndex - (_modeRowCount - _rowExtraCount))
                    }

                    function _rowForModeIndex(modeIndex) {
                        for (var rowIndex = 0; rowIndex < _modeRowCount; rowIndex++) {
                            if (modeIndex < _rowStartIndex(rowIndex) + _rowItemCount(rowIndex)) {
                                return rowIndex
                            }
                        }
                        return Math.max(0, _modeRowCount - 1)
                    }

                    function _modeButtonX(modeIndex) {
                        var rowIndex = _rowForModeIndex(modeIndex)
                        var itemIndex = modeIndex - _rowStartIndex(rowIndex)
                        var itemCount = _rowItemCount(rowIndex)
                        var rowWidth = (itemCount * _modeButtonWidth) +
                                       (Math.max(0, itemCount - 1) * _columnSpacing)
                        return ((_gridWidth - rowWidth) / 2) +
                               (itemIndex * (_modeButtonWidth + _columnSpacing))
                    }

                    function _modeButtonY(modeIndex) {
                        return _rowForModeIndex(modeIndex) * (_rowHeight + _rowSpacing)
                    }

                    QGCPalette { id: qgcPal }

                    Item {
                        id:                     modeGridHost
                        anchors.top:            parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        width:                  flightModePanel._gridWidth
                        height:                 flightModePanel._visibleGridHeight
                        clip:                   true

                        Item {
                            id:                 modeGrid
                            anchors.fill:       parent

                            Repeater {
                                model: flightModePanel._modeList

                                Rectangle {
                                    id: modeRow

                                    property string modeDisplayText: flightModeDisplay.modeText(flightModePanel._activeVehicle, modelData, qsTr("Mode"))

                                    x:          flightModePanel._modeButtonX(index)
                                    y:          flightModePanel._modeButtonY(index)
                                    width:      flightModePanel._modeButtonWidth
                                    height:     flightModePanel._rowHeight
                                    radius:     Math.round(ScreenTools.defaultFontPixelWidth * 0.30)
                                    color:      "transparent"

                                    GlassBackdrop {
                                        anchors.fill:           parent
                                        sourceItem:             dropPanel.backdropSourceItem
                                        targetItem:             modeRow
                                        backdropBlurEnabled:    !!sourceItem
                                        cornerRadius:           modeRow.radius
                                    }

                                    Rectangle {
                                        anchors.fill:   parent
                                        radius:         modeRow.radius
                                        color:          modeMouse.pressed ? Qt.rgba(0.10, 0.56, 0.83, 0.14) :
                                                            (modeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.060) : "transparent")
                                        border.color:   flightModePanel._activeVehicle && flightModePanel._activeVehicle.flightMode === modelData ?
                                                            qgcPal.primaryButton :
                                                            (modeMouse.containsMouse ? Qt.rgba(0.82, 0.90, 0.95, 0.26) : Qt.rgba(0.82, 0.90, 0.95, 0.18))
                                        border.width:   1
                                    }

                                    Item {
                                        id:                 modeTextHost
                                        anchors.fill:           parent
                                        anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 0.72
                                        anchors.rightMargin:    anchors.leftMargin
                                        anchors.topMargin:      ScreenTools.defaultFontPixelHeight * 0.08
                                        anchors.bottomMargin:   anchors.topMargin

                                        RowLayout {
                                            id:                 modeTextRow
                                            anchors.centerIn:   parent
                                            spacing:            modeTypeBadge.visible ? ScreenTools.defaultFontPixelWidth * 0.28 : 0

                                            QGCLabel {
                                                id:                     modeTextLabel
                                                Layout.preferredWidth:  Math.max(0, Math.min(implicitWidth,
                                                                                             modeTextHost.width -
                                                                                             (modeTypeBadge.visible ? modeTypeBadge.width + modeTextRow.spacing : 0)))
                                                Layout.maximumWidth:    Layout.preferredWidth
                                                text:                   flightModeDisplay.labelText(modeRow.modeDisplayText)
                                                color:                  qgcPal.text
                                                font.bold:              flightModePanel._activeVehicle && flightModePanel._activeVehicle.flightMode === modelData
                                                font.pointSize:         ScreenTools.controlFontPointSize
                                                horizontalAlignment:    Text.AlignHCenter
                                                verticalAlignment:      Text.AlignVCenter
                                                fontSizeMode:           Text.HorizontalFit
                                                minimumPointSize:       ScreenTools.captionFontPointSize
                                                elide:                  Text.ElideNone
                                                maximumLineCount:       1
                                            }

                                            Rectangle {
                                                id:                 modeTypeBadge
                                                Layout.alignment:   Qt.AlignVCenter
                                                Layout.preferredWidth: modeTypeBadgeText.implicitWidth + ScreenTools.defaultFontPixelWidth * 0.46
                                                Layout.preferredHeight: Math.max(ScreenTools.defaultFontPixelHeight * 0.70,
                                                                                 modeTypeBadgeText.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.06)
                                                radius:             Math.round(height * 0.28)
                                                color:              Qt.rgba(0.82, 0.88, 0.94, 0.10)
                                                border.color:       Qt.rgba(0.82, 0.88, 0.94, 0.18)
                                                border.width:       1
                                                visible:            modeTypeBadgeText.text !== ""

                                                QGCLabel {
                                                    id:                     modeTypeBadgeText
                                                    anchors.centerIn:       parent
                                                    text:                   flightModeDisplay.badgeText(modeRow.modeDisplayText)
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

                                    QGCMouseArea {
                                        id:             modeMouse
                                        anchors.fill:   parent
                                        hoverEnabled:   !ScreenTools.isMobile
                                        enabled:        flightModePanel._activeVehicle && flightModePanel._activeVehicle.flightModeSetAvailable

                                        onClicked: {
                                            if (flightModePanel._activeVehicle) {
                                                flightModePanel._activeVehicle.flightMode = modelData
                                            }
                                            if (!_root.modePanelPinned) {
                                                dropPanel.hide()
                                            }
                                        }
                                    }

                                    Item {
                                        id:                 favoriteButton
                                        anchors.top:        parent.top
                                        anchors.right:      parent.right
                                        width:              flightModePanel._rowHeight
                                        height:             flightModePanel._rowHeight
                                        z:                  4

                                        property bool favorite: _root.favoriteModeNames.indexOf(modelData) !== -1

                                        QGCColoredImage {
                                            anchors.top:        parent.top
                                            anchors.right:      parent.right
                                            anchors.topMargin:  ScreenTools.defaultFontPixelHeight * 0.16
                                            anchors.rightMargin:ScreenTools.defaultFontPixelWidth * 0.24
                                            width:              ScreenTools.defaultFontPixelHeight * 0.68
                                            height:             width
                                            source:             favoriteButton.favorite ?
                                                                    "/InstrumentValueIcons/star-full.svg" :
                                                                    "/InstrumentValueIcons/star-outline.svg"
                                            color:              favoriteButton.favorite ? qgcPal.colorYellow : qgcPal.buttonText
                                            opacity:            favoriteButton.favorite || favoriteMouse.containsMouse ? 1.0 : 0.66
                                        }

                                        QGCMouseArea {
                                            id:             favoriteMouse
                                            anchors.fill:   parent
                                            hoverEnabled:   !ScreenTools.isMobile
                                            onClicked:      _root.toggleFavoriteMode(modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id:                 pinButton
                        anchors.left:       modeGridHost.right
                        anchors.leftMargin: flightModePanel._pinGap
                        anchors.verticalCenter: modeGridHost.verticalCenter
                        width:              flightModePanel._pinButtonWidth
                        height:             flightModePanel._pinButtonWidth
                        radius:             Math.round(ScreenTools.defaultFontPixelWidth * 0.30)
                        color:              "transparent"

                        GlassBackdrop {
                            anchors.fill:           parent
                            sourceItem:             dropPanel.backdropSourceItem
                            targetItem:             pinButton
                            backdropBlurEnabled:    !!sourceItem
                            cornerRadius:           pinButton.radius
                        }

                        Rectangle {
                            anchors.fill:   parent
                            radius:         pinButton.radius
                            color:          pinMouse.pressed ? Qt.rgba(0.10, 0.56, 0.83, 0.14) :
                                                (pinMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.060) : "transparent")
                            border.color:   pinMouse.containsMouse ? Qt.rgba(0.82, 0.90, 0.95, 0.26) :
                                                Qt.rgba(0.82, 0.90, 0.95, 0.18)
                            border.width:   1
                        }

                        QGCColoredImage {
                            anchors.centerIn:   parent
                            width:              ScreenTools.defaultFontPixelHeight * 0.78
                            height:             width
                            source:             "/InstrumentValueIcons/pin.svg"
                            color:              _root.modePanelPinned ? qgcPal.primaryButton : qgcPal.buttonText
                        }

                        QGCMouseArea {
                            id:             pinMouse
                            anchors.fill:   parent
                            hoverEnabled:   !ScreenTools.isMobile
                            onClicked:      _root.modePanelPinned = !_root.modePanelPinned

                            ToolTip.visible: containsMouse
                            ToolTip.delay:   500
                            ToolTip.text:    _root.modePanelPinned ? qsTr("Unpin mode list") : qsTr("Pin mode list")
                        }
                    }
                }
            }
        },
        GuidedActionTakeoff { },
        GuidedActionLand { },
        GuidedActionPause { },
        GuidedActionContinueMission { },
        GuidedActionRTL { },
        FlyViewAdditionalActionsButton { },
        GuidedActionGripper { }
    ]
}
