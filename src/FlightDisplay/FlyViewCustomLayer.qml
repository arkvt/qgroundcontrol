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
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controllers
import QGroundControl.Controls
import QGroundControl.FactSystem
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.Palette
import QGroundControl.ScreenTools
import QGroundControl.Vehicle

// To implement a custom overlay copy this code to your own control in your custom code source. Then override the
// FlyViewCustomLayer.qml resource with your own qml. See the custom example and documentation for details.
Item {
    id: _root

    property var parentToolInsets               // These insets tell you what screen real estate is available for positioning the controls in your overlay
    property var totalToolInsets:   _toolInsets // These are the insets for your custom overlay additions
    property var mapControl

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property bool _customLandingVisible: customLandingController.modeActive
    readonly property bool _followReturnVisible: customLandingController.followReturnModeActive
                                                 && customLandingController.followReturnPlanValid
    property real _panelMargin: Math.max(8, ScreenTools.defaultFontPixelWidth * 0.75)

    function _queryCustomLandingCapability() {
        if (_activeVehicle && _customLandingVisible) {
            customLandingController.queryCapability()
        }
    }

    on_ActiveVehicleChanged: Qt.callLater(_queryCustomLandingCapability)
    on_CustomLandingVisibleChanged: {
        if (_customLandingVisible) {
            Qt.callLater(_queryCustomLandingCapability)
        }
    }

    Component.onCompleted: Qt.callLater(_queryCustomLandingCapability)

    CustomLandingController {
        id: customLandingController
        vehicle: _root._activeVehicle
    }

    CustomLandingMapVisual {
        id: customLandingMapVisual
        map: _root.mapControl
        controller: customLandingController
        active: _root._customLandingVisible
    }

    // FOLLOW_RETURN reuses the same constrained loiter/tangent geometry as
    // CUSTOM_LAND, but its plan is generated inside the flight controller and
    // must remain read-only in QGC.
    QtObject {
        id: followReturnVisualController

        readonly property var loiterCoordinate: customLandingController.followReturnLoiterCoordinate
        readonly property var landingCoordinate: customLandingController.followReturnLandingCoordinate
        readonly property real loiterAltitude: customLandingController.followReturnLoiterAltitude
        readonly property real landingAltitude: 0
        readonly property real landingElevation: landingCoordinate && landingCoordinate.isValid
                                                      ? Number(landingCoordinate.altitude)
                                                      : NaN
        readonly property real loiterHeightAboveLanding: customLandingController.followReturnLoiterAltitude
        readonly property real loiterRadius: customLandingController.followReturnRadius
        readonly property real airbrakeRadius: 0
        readonly property real tangentDistance: customLandingController.followReturnTangentDistance
        readonly property real approachAirspeed: 0
        readonly property bool clockwise: customLandingController.followReturnClockwise
        readonly property bool safeClimbRequired: customLandingController.followReturnSafeClimbRequired
        readonly property bool modeActive: false
        readonly property bool capabilitySupported: false
        readonly property bool busy: false
        readonly property bool planCommitted: true
    }

    CustomLandingMapVisual {
        id: followReturnMapVisual
        map: _root.mapControl
        controller: followReturnVisualController
        active: _root._followReturnVisible
        entryCoordinate: customLandingController.followReturnEntryCoordinate
        readOnly: true
        showConstraintCircle: false
        showEntryLoiterCircle: followReturnVisualController.safeClimbRequired
        routePathColor: "#ffd400"
        routePathWidth: 12
        routePathOpacity: 0.65
    }

    CustomLandingPanel {
        id: customLandingPanel
        anchors.right: parent.right
        anchors.rightMargin: _root.parentToolInsets.rightEdgeCenterInset + _root._panelMargin
        anchors.verticalCenter: parent.verticalCenter
        z: QGroundControl.zOrderTopMost
        visible: _root._customLandingVisible

        controller: customLandingController
        loiterCoordinateValid: customLandingMapVisual.loiterCoordinateValid
        landingCoordinateValid: customLandingMapVisual.landingCoordinateValid
        geometryValid: customLandingMapVisual.geometryValid
        geometryError: customLandingMapVisual.geometryError
        backdropSourceItem: _root.mapControl
        maximumHeight: Math.max(ScreenTools.minTouchPixels * 5, parent.height - (_root._panelMargin * 2))
    }

    // since this file is a placeholder for the custom layer in a standard build, we will just pass through the parent insets
    QGCToolInsets {
        id:                     _toolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    parentToolInsets.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
                                    + (customLandingPanel.visible ? customLandingPanel.width + (_root._panelMargin * 2) : 0)
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
    }
}
