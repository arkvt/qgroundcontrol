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
import QtQuick.Dialogs
import QtLocation
import QtPositioning
import QtQuick.Shapes

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap
import QGroundControl.ScreenTools

/// Map editing and preview visuals for the in-flight Custom Landing mode.
/// Coordinates are changed locally through CustomLandingController. No command
/// is sent to the vehicle until CustomLandingPanel calls controller.execute().
Item {
    id: _root

    property var map
    property var controller
    property bool active: false
    property var entryCoordinate: QtPositioning.coordinate()
    property bool readOnly: false
    property bool showConstraintCircle: true
    property bool showEntryLoiterCircle: false
    property color routePathColor: QGroundControl.globalPalette.mapMissionTrajectory
    property real routePathWidth: 3
    property real routePathOpacity: 1.0

    readonly property bool loiterCoordinateValid: _coordinateValid(controller ? controller.loiterCoordinate : undefined)
    readonly property bool landingCoordinateValid: _coordinateValid(controller ? controller.landingCoordinate : undefined)
    readonly property bool entryCoordinateValid: _coordinateValid(entryCoordinate)
    readonly property bool draftComplete: loiterCoordinateValid && landingCoordinateValid
    readonly property bool interactive: !readOnly && active && controller && controller.modeActive && controller.capabilitySupported
                                        && !controller.busy && !controller.planCommitted
    readonly property real loiterRadiusMeters: controller ? Math.max(0, Number(controller.loiterRadius)) : 0
    readonly property real tangentDistanceMeters: controller ? Number(controller.tangentDistance) : 300
    readonly property real airbrakeRadiusMeters: controller ? Math.max(0, Number(controller.airbrakeRadius)) : 0
    readonly property real loiterCenterDistanceMeters: loiterRadiusMeters > 0 && tangentDistanceMeters > 0
                                                         ? Math.sqrt(loiterRadiusMeters * loiterRadiusMeters
                                                                     + tangentDistanceMeters * tangentDistanceMeters)
                                                         : 0
    readonly property real geometryToleranceMeters: 1
    readonly property real centerToLandingDistance: draftComplete
                                                        ? controller.loiterCoordinate.distanceTo(controller.landingCoordinate)
                                                        : 0
    readonly property bool geometryValid: draftComplete && loiterRadiusMeters > 0
                                             && tangentDistanceMeters >= 30 && tangentDistanceMeters <= 5000
                                             && Math.abs(centerToLandingDistance - loiterCenterDistanceMeters)
                                                    <= geometryToleranceMeters
    readonly property string geometryError: draftComplete && !geometryValid
                                                ? qsTr("Loiter descent point must remain on the fixed CLND_TAN_DIST constraint.")
                                                : ""
    readonly property var tangentCoordinate: _calculateTangentCoordinate()
    readonly property var directionArrowCoordinate: _circleCoordinate(0)
    readonly property var airbrakeLabelCoordinate: landingCoordinateValid && airbrakeRadiusMeters > 0
                                                        ? controller.landingCoordinate.atDistanceAndAzimuth(
                                                              airbrakeRadiusMeters, 0)
                                                        : QtPositioning.coordinate()
    readonly property var approachPath: geometryValid
                                            ? [tangentCoordinate, controller.landingCoordinate]
                                            : []
    readonly property var returnCircleEntryCoordinate: _calculateReturnCircleEntryCoordinate()
    readonly property var returnStartCoordinate: _calculateReturnStartCoordinate()
    readonly property var returnPath: geometryValid && entryCoordinateValid
                                          && _coordinateValid(returnStartCoordinate)
                                          && _coordinateValid(returnCircleEntryCoordinate)
                                      ? [returnStartCoordinate, returnCircleEntryCoordinate]
                                      : []

    readonly property int   loiterPathWidth:   32
    readonly property color loiterPathColor:   "#ffd400"
    readonly property real  loiterPathOpacity: 0.40

    property var _mapClickArea
    property var _loiterMarker
    property var _landingMarker
    property var _loiterDragArea
    property var _landingDragArea
    property int _selectedMarker: 0
    property var _confirmationDialog
    property bool _abortCommandPending: false

    QGCDynamicObjectManager {
        id: visualObjectManager
    }

    function _coordinateValid(coordinate) {
        return coordinate !== undefined && coordinate !== null && coordinate.isValid
    }

    function _openAbortConfirmation() {
        if (!active || !controller || !controller.planCommitted || controller.busy || _confirmationDialog) {
            return
        }

        _confirmationDialog = abortConfirmationComponent.createObject(mainWindow)
        _confirmationDialog.open()
    }

    function _normalizedBearing(bearing) {
        var normalized = bearing % 360
        return normalized < 0 ? normalized + 360 : normalized
    }

    // The fixed tangent length and loiter radius form a right triangle. The
    // sign selects the tangent whose direction of travel points toward P.
    function _calculateTangentCoordinate() {
        if (!geometryValid) {
            return QtPositioning.coordinate()
        }

        var center = controller.loiterCoordinate
        var landing = controller.landingCoordinate
        var offsetDegrees = Math.atan2(tangentDistanceMeters, loiterRadiusMeters) * 180 / Math.PI
        var centerToLandingBearing = center.azimuthTo(landing)
        var tangentBearing = controller.clockwise
                ? centerToLandingBearing - offsetDegrees
                : centerToLandingBearing + offsetDegrees
        var tangent = center.atDistanceAndAzimuth(loiterRadiusMeters, _normalizedBearing(tangentBearing))
        tangent.altitude = Number(controller.loiterAltitude)
        return tangent
    }

    function _calculateReturnCircleEntryCoordinate() {
        if (!geometryValid || !entryCoordinateValid) {
            return QtPositioning.coordinate()
        }

        var center = controller.loiterCoordinate
        var entryBearing = center.azimuthTo(entryCoordinate)
        var circleEntry = center.atDistanceAndAzimuth(loiterRadiusMeters,
                                                       _normalizedBearing(entryBearing))
        circleEntry.altitude = Number(controller.loiterAltitude)
        return circleEntry
    }

    function _calculateReturnStartCoordinate() {
        if (!entryCoordinateValid) {
            return QtPositioning.coordinate()
        }
        if (!showEntryLoiterCircle || !loiterCoordinateValid || loiterRadiusMeters <= 0) {
            return entryCoordinate
        }

        var departureBearing = entryCoordinate.azimuthTo(controller.loiterCoordinate)
        var departure = entryCoordinate.atDistanceAndAzimuth(loiterRadiusMeters,
                                                              _normalizedBearing(departureBearing))
        departure.altitude = entryCoordinate.altitude
        return departure
    }

    function _circleCoordinate(bearing) {
        if (!loiterCoordinateValid || loiterRadiusMeters <= 0) {
            return QtPositioning.coordinate()
        }
        var coordinate = controller.loiterCoordinate.atDistanceAndAzimuth(loiterRadiusMeters,
                                                                           _normalizedBearing(bearing))
        coordinate.altitude = Number(controller.loiterAltitude)
        return coordinate
    }

    function _roundedMapCoordinate(point, altitude) {
        var coordinate = map.toCoordinate(point, false /* clipToViewport */)
        coordinate.latitude = Number(coordinate.latitude.toFixed(7))
        coordinate.longitude = Number(coordinate.longitude.toFixed(7))
        coordinate.altitude = Number(altitude)
        return coordinate
    }

    function _showVisuals() {
        if (!active || !map || visualObjectManager.rgDynamicObjects.length > 0) {
            return
        }

        _loiterMarker = visualObjectManager.createObject(loiterMarkerComponent, map, true /* parentObjectIsMap */)
        _landingMarker = visualObjectManager.createObject(landingMarkerComponent, map, true /* parentObjectIsMap */)
        visualObjectManager.createObjects([
            tangentDistanceCircleComponent,
            entryLoiterCircleComponent,
            loiterCircleComponent,
            returnLineComponent,
            directionArrowComponent,
            airbrakeCircleComponent,
            airbrakeLabelComponent,
            approachLineComponent,
            tangentMarkerComponent
        ], map, true /* parentObjectIsMap */)

        _syncDragAreas()
    }

    function _hideVisuals() {
        _hideMapClickArea()
        _hideDragAreas()
        visualObjectManager.destroyObjects()
        _loiterMarker = undefined
        _landingMarker = undefined
    }

    function _syncDragAreas() {
        if (!active || !map || !_loiterMarker || !_landingMarker) {
            return
        }

        if (!interactive) {
            _hideDragAreas()
            return
        }

        // Create drag handles only after their coordinates are valid. Apart
        // from avoiding an invalid initial map position, recreating the handle
        // after Reset also restores bindings which MouseArea.drag necessarily
        // breaks while moving the transparent drag item.
        if (loiterCoordinateValid) {
            if (!_loiterDragArea) {
                _loiterDragArea = loiterDragAreaComponent.createObject(map)
            }
        } else {
            if (_loiterDragArea) {
                _loiterDragArea.destroy()
                _loiterDragArea = undefined
            }
        }

        if (landingCoordinateValid) {
            if (!_landingDragArea) {
                _landingDragArea = landingDragAreaComponent.createObject(map)
            }
        } else if (_landingDragArea) {
            _landingDragArea.destroy()
            _landingDragArea = undefined
        }
    }

    function _hideDragAreas() {
        if (_loiterDragArea) {
            _loiterDragArea.destroy()
            _loiterDragArea = undefined
        }
        if (_landingDragArea) {
            _landingDragArea.destroy()
            _landingDragArea = undefined
        }
    }

    function _recreateLoiterDragArea() {
        if (_loiterDragArea) {
            _loiterDragArea.destroy()
            _loiterDragArea = undefined
        }
        Qt.callLater(_syncDragAreas)
    }

    function _recreateDragAreas() {
        _hideDragAreas()
        Qt.callLater(_syncDragAreas)
    }

    function _showMapClickArea() {
        if (!_mapClickArea && active && interactive && (!loiterCoordinateValid || !landingCoordinateValid)) {
            _mapClickArea = mapClickAreaComponent.createObject(map)
        }
    }

    function _hideMapClickArea() {
        if (_mapClickArea) {
            _mapClickArea.destroy()
            _mapClickArea = undefined
        }
    }

    function _updateInteractionObjects() {
        if (!active) {
            _hideVisuals()
            return
        }

        _showVisuals()
        _syncDragAreas()
        if (interactive && (!loiterCoordinateValid || !landingCoordinateValid)) {
            _showMapClickArea()
        } else {
            _hideMapClickArea()
        }
    }

    onActiveChanged: {
        if (!active) {
            _selectedMarker = 0
            _abortCommandPending = false
            if (_confirmationDialog) {
                _confirmationDialog.close()
                _confirmationDialog = undefined
            }
        }
        Qt.callLater(_updateInteractionObjects)
    }
    onMapChanged: Qt.callLater(function() {
        _hideVisuals()
        _updateInteractionObjects()
    })
    onInteractiveChanged: Qt.callLater(_updateInteractionObjects)
    onLoiterCoordinateValidChanged: Qt.callLater(_updateInteractionObjects)
    onLandingCoordinateValidChanged: Qt.callLater(_updateInteractionObjects)

    Component.onCompleted: _updateInteractionObjects()
    Component.onDestruction: _hideVisuals()

    Connections {
        target: controller
        ignoreUnknownSignals: true

        function onLoiterCoordinateChanged() {
            Qt.callLater(_root._updateInteractionObjects)
        }

        function onLandingCoordinateChanged() {
            Qt.callLater(_root._updateInteractionObjects)
        }

        function onBusyChanged() {
            Qt.callLater(_root._updateInteractionObjects)

            if (!_root._abortCommandPending || !_root.controller || _root.controller.busy) {
                return
            }

            if (_root.controller.planCommitted && _root.controller.errorText.length > 0) {
                mainWindow.showMessageDialog(
                            qsTranslate("CustomLandingPanel", "Abort landing"),
                            _root.controller.errorText)
            }
            _root._abortCommandPending = false
        }

        function onPlanCommittedChanged() {
            Qt.callLater(_root._updateInteractionObjects)
        }
    }

    Component {
        id: mapClickAreaComponent

        MouseArea {
            anchors.fill: map
            z: QGroundControl.zOrderMapItems + 20
            acceptedButtons: Qt.LeftButton
            visible: _root.active && _root.interactive
                     && (!_root.loiterCoordinateValid || !_root.landingCoordinateValid)

            onClicked: (mouse) => {
                if (!_root.landingCoordinateValid) {
                    _root.controller.landingCoordinate = _root._roundedMapCoordinate(
                                Qt.point(mouse.x, mouse.y), _root.controller.landingAltitude)
                } else if (!_root.loiterCoordinateValid) {
                    _root.controller.loiterCoordinate = _root._roundedMapCoordinate(
                                Qt.point(mouse.x, mouse.y), _root.controller.loiterAltitude)
                }
                Qt.callLater(_root._updateInteractionObjects)
            }
        }
    }

    Component {
        id: loiterDragAreaComponent

        MissionItemIndicatorDrag {
            mapControl: map
            itemIndicator: _root._loiterMarker
            itemCoordinate: _root.controller ? _root.controller.loiterCoordinate : QtPositioning.coordinate()
            // The full-map click catcher remains active until both points are
            // selected. Keep an existing marker above it so it can already be
            // refined while the other point is still unset.
            z: QGroundControl.zOrderMapItems + 30
            visible: _root.interactive && _root.loiterCoordinateValid

            onClicked: _root._selectedMarker = _root._selectedMarker === 2 ? 0 : 2

            onItemCoordinateChanged: {
                if (_root.interactive && _root._coordinateValid(itemCoordinate)) {
                    var coordinate = itemCoordinate
                    coordinate.altitude = Number(_root.controller.loiterAltitude)
                    _root.controller.loiterCoordinate = coordinate
                }
            }

            // The C++ controller projects the dragged loiter point onto the
            // fixed-radius circle around the vertical landing point. Recreate
            // the transparent handle so it snaps back onto the marker after
            // MouseArea.drag broke the itemCoordinate binding.
            onDragStop: _root._recreateLoiterDragArea()
        }
    }

    Component {
        id: landingDragAreaComponent

        MissionItemIndicatorDrag {
            mapControl: map
            itemIndicator: _root._landingMarker
            itemCoordinate: _root.controller ? _root.controller.landingCoordinate : QtPositioning.coordinate()
            z: QGroundControl.zOrderMapItems + 30
            visible: _root.interactive && _root.landingCoordinateValid

            onClicked: _root._selectedMarker = _root._selectedMarker === 1 ? 0 : 1

            onItemCoordinateChanged: {
                if (_root.interactive && _root._coordinateValid(itemCoordinate)) {
                    var coordinate = itemCoordinate
                    coordinate.altitude = Number(_root.controller.landingAltitude)
                    _root.controller.landingCoordinate = coordinate
                }
            }

            // The landing point is the anchor. Moving it translates the
            // constrained loiter point while preserving the approach bearing.
            onDragStop: _root._recreateDragAreas()
        }
    }

    Component {
        id: tangentDistanceCircleComponent

        MapCircle {
            z: QGroundControl.zOrderMapItems - 3
            center: _root.controller ? _root.controller.landingCoordinate : QtPositioning.coordinate()
            // CLND_TAN_DIST constrains the tangent exit point, not the loiter
            // center. The center itself remains sqrt(R^2 + D^2) away.
            radius: _root.tangentDistanceMeters
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.55)
            color: "transparent"
            visible: _root.active && _root.showConstraintCircle && _root.landingCoordinateValid
                     && _root.tangentDistanceMeters > 0
        }
    }

    Component {
        id: entryLoiterCircleComponent

        MapCircle {
            z: QGroundControl.zOrderMapItems - 2
            center: _root.entryCoordinate
            radius: _root.loiterRadiusMeters
            border.width: _root.loiterPathWidth
            border.color: _root.loiterPathColor
            color: "transparent"
            opacity: _root.loiterPathOpacity
            visible: _root.active && _root.showEntryLoiterCircle
                     && _root.entryCoordinateValid && _root.loiterRadiusMeters > 0
        }
    }

    Component {
        id: loiterCircleComponent

        MapCircle {
            z: QGroundControl.zOrderMapItems - 2
            center: _root.controller ? _root.controller.loiterCoordinate : QtPositioning.coordinate()
            radius: _root.loiterRadiusMeters
            border.width: _root.loiterPathWidth
            border.color: _root.loiterPathColor
            color: "transparent"
            opacity: _root.loiterPathOpacity
            visible: _root.active && _root.loiterCoordinateValid && _root.loiterRadiusMeters > 0
        }
    }

    Component {
        id: returnLineComponent

        MapPolyline {
            z: QGroundControl.zOrderMapItems - 1
            line.color: _root.routePathColor
            line.width: _root.routePathWidth
            opacity: _root.routePathOpacity
            path: _root.returnPath
            visible: _root.active && _root.returnPath.length === 2
        }
    }

    Component {
        id: directionArrowComponent

        MapQuickItem {
            z: QGroundControl.zOrderMapItems
            coordinate: _root.directionArrowCoordinate
            anchorPoint.x: sourceItem.width / 2
            anchorPoint.y: sourceItem.height / 2
            visible: _root.active && _root.loiterCoordinateValid && _root.loiterRadiusMeters > 0

            sourceItem: Shape {
                width: Math.max(16, ScreenTools.defaultFontPixelHeight * 0.95)
                height: width * 0.68

                transform: Rotation {
                    origin.x: width / 2
                    origin.y: height / 2
                    angle: (_root.controller && _root.controller.clockwise ? 180 : 0)
                           - (_root.map && isFinite(Number(_root.map.bearing)) ? Number(_root.map.bearing) : 0)
                }

                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillColor: QGroundControl.globalPalette.mapMissionTrajectory
                    startX: 0
                    startY: height / 2
                    PathLine { x: width; y: height }
                    PathLine { x: width; y: 0 }
                    PathLine { x: 0; y: height / 2 }
                }
            }
        }
    }

    Component {
        id: approachLineComponent

        MapPolyline {
            z: QGroundControl.zOrderMapItems - 1
            line.color: _root.loiterPathColor
            line.width: _root.loiterPathWidth
            opacity: _root.loiterPathOpacity
            path: _root.approachPath
            visible: _root.active && _root.geometryValid
        }
    }

    Component {
        id: airbrakeCircleComponent

        MapCircle {
            z: QGroundControl.zOrderMapItems - 2
            center: _root.controller ? _root.controller.landingCoordinate : QtPositioning.coordinate()
            radius: _root.airbrakeRadiusMeters
            border.width: 1
            border.color: Qt.rgba(1.0, 0.62, 0.26, 0.45)
            color: "transparent"
            visible: _root.active && _root.landingCoordinateValid && _root.airbrakeRadiusMeters > 0
        }
    }

    Component {
        id: airbrakeLabelComponent

        MapQuickItem {
            z: QGroundControl.zOrderMapItems
            coordinate: _root.airbrakeLabelCoordinate
            anchorPoint.x: sourceItem.width / 2
            anchorPoint.y: sourceItem.height / 2
            visible: _root.active && _root.landingCoordinateValid && _root.airbrakeRadiusMeters > 0

            sourceItem: QGCLabel {
                text: qsTr("Deceleration zone")
                color: Qt.rgba(1.0, 0.72, 0.42, 0.82)
                font.pointSize: ScreenTools.smallFontPointSize
                style: Text.Outline
                styleColor: "black"
            }
        }
    }

    Component {
        id: tangentMarkerComponent

        MapQuickItem {
            z: QGroundControl.zOrderMapItems
            coordinate: _root.tangentCoordinate
            anchorPoint.x: sourceItem.width / 2
            anchorPoint.y: sourceItem.height / 2
            visible: _root.active && _root.geometryValid

            sourceItem: Rectangle {
                width: Math.max(8, ScreenTools.defaultFontPixelWidth * 0.75)
                height: width
                radius: width / 2
                color: QGroundControl.globalPalette.mapMissionTrajectory
                border.width: 1
                border.color: "white"
            }
        }
    }

    Component {
        id: loiterMarkerComponent

        MapQuickItem {
            id: loiterMapItem

            z: QGroundControl.zOrderMapItems + 1
            coordinate: _root.controller ? _root.controller.loiterCoordinate : QtPositioning.coordinate()
            anchorPoint.x: loiterMarkerSource.anchorPointX
            anchorPoint.y: loiterMarkerSource.anchorPointY
            visible: _root.active && _root.loiterCoordinateValid

            sourceItem: MissionItemIndexLabel {
                id: loiterMarkerSource
                index: 2
                label: qsTr("Loiter descent") + "  +"
                       + Number(_root.controller.loiterHeightAboveLanding).toFixed(1) + " " + qsTr("m")
                checked: _root._selectedMarker === 2
                trailingActionVisible: _root.controller && _root.controller.planCommitted
                trailingActionIconSource: "/res/cancel.svg"
                trailingActionToolTip: qsTranslate("CustomLandingPanel", "Abort landing")
                onClicked: _root._selectedMarker = _root._selectedMarker === 2 ? 0 : 2
                onTrailingActionClicked: _root._openAbortConfirmation()
            }
        }
    }

    Component {
        id: landingMarkerComponent

        MapQuickItem {
            id: landingMapItem

            z: QGroundControl.zOrderMapItems + 1
            coordinate: _root.controller ? _root.controller.landingCoordinate : QtPositioning.coordinate()
            anchorPoint.x: landingMarkerSource.anchorPointX
            anchorPoint.y: landingMarkerSource.anchorPointY
            visible: _root.active && _root.landingCoordinateValid

            sourceItem: MissionItemIndexLabel {
                id: landingMarkerSource
                index: 1
                label: qsTr("Vertical land") + "  "
                       + (isFinite(Number(_root.controller.landingElevation))
                              ? Number(_root.controller.landingElevation).toFixed(1) + " " + qsTr("m AMSL")
                              : qsTr("Not set"))
                checked: _root._selectedMarker === 1
                trailingActionVisible: _root.controller && _root.controller.planCommitted
                trailingActionIconSource: "/res/cancel.svg"
                trailingActionToolTip: qsTranslate("CustomLandingPanel", "Abort landing")
                onClicked: _root._selectedMarker = _root._selectedMarker === 1 ? 0 : 1
                onTrailingActionClicked: _root._openAbortConfirmation()
            }
        }
    }

    Component {
        id: abortConfirmationComponent

        QGCPopupDialog {
            title: qsTranslate("CustomLandingPanel", "Abort Custom Landing?")
            buttons: Dialog.Yes | Dialog.Cancel

            onAccepted: {
                _root._abortCommandPending = true
                _root.controller.cancel()
            }
            onClosed: _root._confirmationDialog = undefined

            QGCLabel {
                text: qsTranslate(
                          "CustomLandingPanel",
                          "Cancel is accepted before VTOL approach starts and then holds in Custom Landing. After VTOL approach starts it is denied; explicitly select QLOITER or QRTL if an abort is required.")
                wrapMode: Text.WordWrap
            }
        }
    }
}
