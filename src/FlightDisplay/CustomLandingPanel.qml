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

import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

/// Operator panel for selecting, reviewing and committing an in-flight custom
/// landing. Editing only changes CustomLandingController's local draft.
Rectangle {
    id: _root

    property var controller
    property bool loiterCoordinateValid: false
    property bool landingCoordinateValid: false
    property bool geometryValid: true
    property string geometryError
    property var backdropSourceItem
    property real maximumHeight: parent ? parent.height : 0

    readonly property bool draftComplete: loiterCoordinateValid && landingCoordinateValid
    readonly property bool draftEditable: controller && controller.modeActive && controller.capabilitySupported
                                           && !controller.busy && !controller.planCommitted
    readonly property bool executionAllowed: controller && controller.modeActive && controller.capabilitySupported
                                             && controller.canExecute && draftComplete && geometryValid
                                             && !controller.busy && !controller.planCommitted
    readonly property string instructionText: {
        if (!controller) {
            return qsTr("Waiting for the custom landing controller.")
        }
        if (!controller.modeActive) {
            return qsTr("Waiting for the vehicle to confirm Custom Landing mode.")
        }
        if (!controller.capabilitySupported) {
            return qsTr("Checking whether the vehicle supports Custom Landing.")
        }
        if (controller.planCommitted) {
            return qsTr("The landing plan is committed. Map editing is locked.")
        }
        if (controller.busy) {
            return qsTr("Communicating with the vehicle. Keep the aircraft in Custom Landing mode.")
        }
        if (!landingCoordinateValid) {
            return qsTr("Click the map to set the vertical landing point.")
        }
        if (!loiterCoordinateValid) {
            return qsTr("Click the map to set the loiter descent point.")
        }
        return qsTr("Drag the loiter marker around the vertical landing point to choose the approach direction, then review and execute.")
    }

    property var _confirmationDialog
    property real _margin: Math.max(7, ScreenTools.defaultFontPixelWidth * 0.6)
    readonly property color _accentColor: qgcPal.primaryButton

    width: Math.max(300, ScreenTools.defaultFontPixelWidth * 31)
    height: Math.min(contentColumn.implicitHeight + (_margin * 2),
                     Math.max(ScreenTools.minTouchPixels * 5, maximumHeight))
    radius: Math.round(ScreenTools.defaultFontPixelWidth * 0.8)
    color: "transparent"
    border.width: 1
    border.color: Qt.rgba(0.82, 0.90, 0.95, 0.14)
    clip: true

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: true
    }

    GlassBackdrop {
        anchors.fill: parent
        sourceItem: _root.backdropSourceItem
        backdropBlurEnabled: true
        targetItem: _root
        cornerRadius: _root.radius
    }

    // The panel itself must consume clicks, while the rest of the custom layer
    // remains transparent to normal map interaction.
    DeadMouseArea {
        anchors.fill: parent
    }

    function _coordinateText(coordinate) {
        if (!coordinate || !coordinate.isValid) {
            return qsTr("Not set")
        }
        return Number(coordinate.latitude).toFixed(7) + ", "
                + Number(coordinate.longitude).toFixed(7)
    }

    function _numberText(value, decimals) {
        var numericValue = Number(value)
        return isFinite(numericValue) ? numericValue.toFixed(decimals) : "--"
    }

    function _parsedNumber(text, fallback) {
        var value = parseFloat(String(text).replace(",", "."))
        return isFinite(value) ? value : fallback
    }

    function _homeRelativeAltitudeText(value) {
        var numericValue = Number(value)
        if (!isFinite(numericValue)) {
            return qsTr("Home") + " --"
        }
        var sign = numericValue >= 0 ? "+" : "-"
        return qsTr("Home") + sign + Math.abs(numericValue).toFixed(1) + " " + qsTr("m")
    }

    function _openExecuteConfirmation() {
        if (!executionAllowed || _confirmationDialog) {
            return
        }
        _confirmationDialog = executeConfirmationComponent.createObject(mainWindow)
        _confirmationDialog.open()
    }

    onVisibleChanged: {
        if (!visible && _confirmationDialog) {
            _confirmationDialog.close()
            _confirmationDialog = undefined
        }
    }

    component SectionDivider: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Qt.rgba(1, 1, 1, 0.15)
    }

    component NumericFieldRow: RowLayout {
        id: numericRow

        property string label
        property real value: 0
        property string units
        property real minimumValue: -100000
        property real maximumValue: 100000
        property bool editable: true
        property bool allowUnset: false

        signal valueEdited(real newValue)

        function formattedValue() {
            var numericValue = Number(value)
            if (allowUnset && (!isFinite(numericValue) || numericValue <= 0)) {
                return ""
            }
            return _root._numberText(numericValue, 1)
        }

        Layout.fillWidth: true
        spacing: ScreenTools.defaultFontPixelWidth * 0.5

        QGCLabel {
            Layout.fillWidth: true
            text: numericRow.label
            color: qgcPal.text
        }

        QGCTextField {
            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 10.5
            Layout.minimumWidth: Layout.preferredWidth
            horizontalAlignment: TextInput.AlignRight
            text: numericRow.formattedValue()
            placeholderText: numericRow.allowUnset ? qsTr("Default") : ""
            enabled: numericRow.editable
            numericValuesOnly: true
            showUnits: true
            unitsLabel: numericRow.units

            validator: DoubleValidator {
                bottom: numericRow.minimumValue
                top: numericRow.maximumValue
                decimals: 1
                notation: DoubleValidator.StandardNotation
            }

            onEditingFinished: {
                var parsedValue = numericRow.allowUnset && String(text).trim().length === 0
                        ? NaN
                        : _root._parsedNumber(text, numericRow.value)
                if (isFinite(parsedValue)) {
                    parsedValue = Math.max(numericRow.minimumValue,
                                           Math.min(numericRow.maximumValue, parsedValue))
                }
                numericRow.valueEdited(parsedValue)
                text = Qt.binding(function() { return numericRow.formattedValue() })
                focus = false
            }
        }
    }

    component PointBadge: Rectangle {
        property int pointIndex: 1
        property bool selected: false

        implicitWidth: Math.max(ScreenTools.defaultFontPixelHeight * 1.45, ScreenTools.minTouchPixels * 0.62)
        implicitHeight: implicitWidth
        radius: width / 2
        color: Qt.rgba(1, 1, 1, 0.10)
        border.width: 1
        border.color: selected ? Qt.rgba(1, 1, 1, 0.65) : Qt.rgba(1, 1, 1, 0.18)

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.55)
            visible: parent.selected
        }

        QGCLabel {
            anchors.fill: parent
            text: parent.pointIndex
            color: "white"
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component PointDeleteButton: QGCButton {
        id: deleteButton

        property bool pointSet: false
        property string toolTipText

        signal removeClicked()

        readonly property real buttonSize: Math.round(ScreenTools.defaultFontPixelHeight * 1.55)

        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        Layout.minimumWidth: buttonSize
        Layout.maximumWidth: buttonSize
        Layout.preferredWidth: buttonSize
        Layout.minimumHeight: buttonSize
        Layout.maximumHeight: buttonSize
        Layout.preferredHeight: buttonSize
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        iconSource: "/res/TrashDelete.svg"
        glassStyle: true
        visible: pointSet
        enabled: pointSet && _root.draftEditable
        ToolTip.visible: hovered
        ToolTip.text: toolTipText
        onClicked: removeClicked()
    }

    Flickable {
        id: panelFlickable
        anchors.fill: parent
        anchors.margins: _root._margin
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: contentColumn
            width: panelFlickable.width
            spacing: ScreenTools.defaultFontPixelHeight * 0.36

            RowLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth * 0.6

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("Custom Landing")
                    font.bold: true
                    font.pointSize: ScreenTools.titleFontPointSize
                    color: "white"
                }

                BusyIndicator {
                    Layout.preferredWidth: ScreenTools.minTouchPixels * 0.65
                    Layout.preferredHeight: width
                    running: controller ? controller.busy : false
                    visible: running
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                text: _root.instructionText
                wrapMode: Text.WordWrap
                color: "white"
            }

            QGCLabel {
                Layout.fillWidth: true
                visible: controller && !controller.capabilitySupported && !controller.busy
                text: qsTr("The connected firmware did not report Custom Landing capability.")
                wrapMode: Text.WordWrap
                color: qgcPal.warningText
            }

            QGCButton {
                Layout.fillWidth: true
                glassStyle: true
                visible: controller && !controller.capabilitySupported && !controller.busy
                text: qsTr("Check capability again")
                onClicked: controller.queryCapability()
            }

            QGCLabel {
                Layout.fillWidth: true
                visible: controller && controller.errorText.length > 0
                text: controller ? controller.errorText : ""
                wrapMode: Text.WordWrap
                color: qgcPal.warningText
                font.bold: true
            }

            QGCLabel {
                Layout.fillWidth: true
                visible: _root.geometryError.length > 0
                text: _root.geometryError
                wrapMode: Text.WordWrap
                color: qgcPal.warningText
                font.bold: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: landingCardColumn.implicitHeight + (_root._margin * 1.5)
                radius: ScreenTools.defaultFontPixelWidth * 0.5
                color: Qt.rgba(1, 1, 1, 0.055)
                border.width: !_root.landingCoordinateValid ? 2 : 1
                border.color: !_root.landingCoordinateValid
                                  ? _root._accentColor
                                  : Qt.rgba(0.82, 0.88, 0.94, 0.12)

                ColumnLayout {
                    id: landingCardColumn
                    anchors.fill: parent
                    anchors.margins: _root._margin * 0.75
                    spacing: ScreenTools.defaultFontPixelHeight * 0.25

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 0.6

                        PointBadge {
                            pointIndex: 1
                            selected: !_root.landingCoordinateValid
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QGCLabel {
                                Layout.fillWidth: true
                                text: qsTr("Vertical landing point")
                                font.bold: true
                                color: qgcPal.text
                            }

                            QGCLabel {
                                Layout.fillWidth: true
                                text: _root._coordinateText(controller ? controller.landingCoordinate : undefined)
                                elide: Text.ElideRight
                                color: Qt.rgba(1, 1, 1, 0.62)
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        QGCButton {
                            readonly property real buttonSize: Math.round(ScreenTools.defaultFontPixelHeight * 1.55)

                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            Layout.minimumWidth: buttonSize
                            Layout.maximumWidth: buttonSize
                            Layout.preferredWidth: buttonSize
                            Layout.minimumHeight: buttonSize
                            Layout.maximumHeight: buttonSize
                            Layout.preferredHeight: buttonSize
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0
                            iconSource: "qrc:/InstrumentValueIcons/refresh.svg"
                            glassStyle: true
                            enabled: _root.draftEditable && controller && controller.rtkAltitudeAvailable
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Sync altitude")
                            onClicked: controller.readCurrentRtkAltitude()
                        }

                        PointDeleteButton {
                            pointSet: _root.landingCoordinateValid
                            toolTipText: qsTr("Delete vertical landing point")
                            onRemoveClicked: controller.clearLandingCoordinate()
                        }
                    }

                    NumericFieldRow {
                        label: qsTr("Landing elevation AMSL")
                        value: controller ? controller.landingElevation : NaN
                        units: qsTr("m")
                        minimumValue: -1000
                        maximumValue: 10000
                        editable: _root.draftEditable
                        onValueEdited: (newValue) => controller.landingElevation = newValue
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 0.5

                        QGCLabel {
                            Layout.fillWidth: true
                            text: qsTr("Synced from RTK by default")
                            color: Qt.rgba(1, 1, 1, 0.58)
                            font.pointSize: ScreenTools.smallFontPointSize
                        }

                        QGCLabel {
                            text: _root._homeRelativeAltitudeText(
                                      controller ? controller.landingAltitude : NaN)
                            color: Qt.rgba(1, 1, 1, 0.68)
                            font.pointSize: ScreenTools.smallFontPointSize
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: loiterCardColumn.implicitHeight + (_root._margin * 1.5)
                radius: ScreenTools.defaultFontPixelWidth * 0.5
                color: Qt.rgba(1, 1, 1, 0.055)
                border.width: _root.landingCoordinateValid && !_root.loiterCoordinateValid ? 2 : 1
                border.color: _root.landingCoordinateValid && !_root.loiterCoordinateValid
                                  ? _root._accentColor
                                  : Qt.rgba(0.82, 0.88, 0.94, 0.12)

                ColumnLayout {
                    id: loiterCardColumn
                    anchors.fill: parent
                    anchors.margins: _root._margin * 0.75
                    spacing: ScreenTools.defaultFontPixelHeight * 0.25

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 0.6

                        PointBadge {
                            pointIndex: 2
                            selected: _root.landingCoordinateValid && !_root.loiterCoordinateValid
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QGCLabel {
                                Layout.fillWidth: true
                                text: qsTr("Loiter descent point")
                                font.bold: true
                                color: qgcPal.text
                            }

                            QGCLabel {
                                Layout.fillWidth: true
                                text: _root._coordinateText(controller ? controller.loiterCoordinate : undefined)
                                elide: Text.ElideRight
                                color: Qt.rgba(1, 1, 1, 0.62)
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        PointDeleteButton {
                            pointSet: _root.loiterCoordinateValid
                            toolTipText: qsTr("Delete loiter descent point")
                            onRemoveClicked: controller.clearLoiterCoordinate()
                        }
                    }

                    NumericFieldRow {
                        label: qsTr("Loiter height above landing")
                        value: controller ? controller.loiterHeightAboveLanding : 50
                        units: qsTr("m")
                        minimumValue: 20
                        maximumValue: 10000
                        editable: _root.draftEditable
                        onValueEdited: (newValue) => controller.loiterHeightAboveLanding = newValue
                    }

                    NumericFieldRow {
                        label: qsTr("Loiter radius")
                        value: controller ? controller.loiterRadius : 100
                        units: qsTr("m")
                        minimumValue: controller ? controller.minimumLoiterRadius : 100
                        maximumValue: 10000
                        editable: _root.draftEditable
                        onValueEdited: (newValue) => controller.loiterRadius = newValue
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        QGCLabel {
                            Layout.fillWidth: true
                            text: qsTr("Loiter direction")
                            color: qgcPal.text
                        }

                        QGCSwitch {
                            text: checked ? qsTr("Clockwise") : qsTr("Counter-clockwise")
                            checked: controller ? controller.clockwise : true
                            enabled: _root.draftEditable
                            onToggled: controller.clockwise = checked
                        }
                    }
                }
            }

            SectionDivider { }

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Landing elevation is converted relative to Home, and loiter height is added above the landing point when uploaded.")
                wrapMode: Text.WordWrap
                color: Qt.rgba(1, 1, 1, 0.58)
                font.pointSize: ScreenTools.smallFontPointSize
            }

            SectionDivider { }

            QGCButton {
                Layout.preferredWidth: contentColumn.width * 0.72
                Layout.minimumWidth: ScreenTools.minTouchPixels * 4
                Layout.alignment: Qt.AlignHCenter
                primary: true
                textColor: "white"
                text: qsTr("Review and execute")
                enabled: _root.executionAllowed
                onClicked: _root._openExecuteConfirmation()
            }
        }
    }

    Component {
        id: executeConfirmationComponent

        QGCPopupDialog {
            id: executeDialog
            title: qsTr("Execute Custom Landing?")
            buttons: Dialog.Yes | Dialog.Cancel
            acceptButtonEnabled: _root.executionAllowed

            onAccepted: {
                if (_root.executionAllowed) {
                    _root.controller.execute()
                }
            }
            onClosed: _root._confirmationDialog = undefined

            ColumnLayout {
                spacing: ScreenTools.defaultFontPixelHeight * 0.6

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("The vehicle will leave its current hold and execute this landing path after the flight controller accepts the plan.")
                    wrapMode: Text.WordWrap
                }

                GridLayout {
                    columns: 2
                    columnSpacing: ScreenTools.defaultFontPixelWidth
                    rowSpacing: ScreenTools.defaultFontPixelHeight * 0.35

                    QGCLabel { text: qsTr("Loiter point") }
                    QGCLabel { text: _root._coordinateText(_root.controller.loiterCoordinate) }
                    QGCLabel { text: qsTr("Loiter height above landing") }
                    QGCLabel { text: qsTr("%1 m").arg(_root._numberText(_root.controller.loiterHeightAboveLanding, 1)) }
                    QGCLabel { text: qsTr("Radius / direction") }
                    QGCLabel {
                        text: qsTr("%1 m, %2").arg(_root._numberText(_root.controller.loiterRadius, 1))
                                                   .arg(_root.controller.clockwise ? qsTr("clockwise")
                                                                                  : qsTr("counter-clockwise"))
                    }
                    QGCLabel { text: qsTr("Landing point") }
                    QGCLabel { text: _root._coordinateText(_root.controller.landingCoordinate) }
                    QGCLabel { text: qsTr("Landing elevation AMSL") }
                    QGCLabel { text: qsTr("%1 m").arg(_root._numberText(_root.controller.landingElevation, 1)) }
                }
            }
        }
    }

}
