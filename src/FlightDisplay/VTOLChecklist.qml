/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.Vehicle

Item {
    id: root

    property var model: listModel

    PreFlightCheckModel {
        id: listModel

        PreFlightCheckGroup {
            name: qsTr("Control Surface Logic - Manual Flight Mode")

            PreFlightManualCheckButton {
                name:       qsTr("1. Move aileron stick left")
                manualText: qsTr("Left aileron up; right aileron down.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("2. Move aileron stick right")
                manualText: qsTr("Left aileron down; right aileron up.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("3. Move elevator stick up")
                manualText: qsTr("Both V-tail surfaces move inward.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("4. Move elevator stick down")
                manualText: qsTr("Both V-tail surfaces move outward.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("5. Move rudder stick left")
                manualText: qsTr("Left tail surface moves upper-left; right tail surface moves lower-left.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("6. Move rudder stick right")
                manualText: qsTr("Left tail surface moves lower-right; right tail surface moves upper-right.")
            }
        }

        PreFlightCheckGroup {
            name: qsTr("Control Surface Logic - Assisted Flight Mode A")

            PreFlightManualCheckButton {
                name:       qsTr("7. Tilt aircraft left")
                manualText: qsTr("Left aileron down; right aileron up.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("8. Tilt aircraft right")
                manualText: qsTr("Left aileron up; right aileron down.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("9. Raise aircraft nose")
                manualText: qsTr("Both V-tail surfaces move inward.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("10. Lower aircraft nose")
                manualText: qsTr("Both V-tail surfaces move outward.")
            }
        }

        PreFlightCheckGroup {
            name: qsTr("Airspeed Check")

            PreFlightManualCheckButton {
                name:       qsTr("11. Do not blow into the airspeed tube")
                manualText: qsTr("Airspeed is 0-2 m/s; it may occasionally jump to 3 m/s or 4 m/s.")
            }
            PreFlightManualCheckButton {
                name:       qsTr("12. Blow directly into the airspeed tube")
                manualText: qsTr("Airspeed increases clearly above 10 m/s.")
            }
        }

        PreFlightCheckGroup {
            name: qsTr("Satellite Count Check")

            PreFlightManualCheckButton {
                name:       qsTr("13. Observe satellite count")
                manualText: qsTr("Satellite count is at least 28.")
            }
        }

        PreFlightCheckGroup {
            name: qsTr("Flight Control Surface Check")

            PreFlightCommandCheckButton {
                name:       qsTr("14. Send nose-up command")
                manualText: qsTr("Both V-tail surfaces move outward.")
                vehicle:    globals.activeVehicle
                testId:     53
            }
            PreFlightCommandCheckButton {
                name:       qsTr("15. Send nose-down command")
                manualText: qsTr("Both V-tail surfaces move inward.")
                vehicle:    globals.activeVehicle
                testId:     54
            }
            PreFlightCommandCheckButton {
                name:       qsTr("16. Send roll-left command")
                manualText: qsTr("Left aileron up; right aileron down.")
                vehicle:    globals.activeVehicle
                testId:     55
            }
            PreFlightCommandCheckButton {
                name:       qsTr("17. Send roll-right command")
                manualText: qsTr("Left aileron down; right aileron up.")
                vehicle:    globals.activeVehicle
                testId:     56
            }
            PreFlightCommandCheckButton {
                name:       qsTr("18. Send yaw-left command")
                manualText: qsTr("Left tail surface moves upper-left; right tail surface moves lower-left.")
                vehicle:    globals.activeVehicle
                testId:     57
            }
            PreFlightCommandCheckButton {
                name:       qsTr("19. Send yaw-right command")
                manualText: qsTr("Left tail surface moves lower-right; right tail surface moves upper-right.")
                vehicle:    globals.activeVehicle
                testId:     58
            }
        }

        PreFlightCheckGroup {
            name: qsTr("Fixed-Wing Throttle Check")

            PreFlightCommandCheckButton {
                name:       qsTr("20. Test fixed-wing throttle")
                manualText: qsTr("Keep the propeller area clear. The vehicle will arm automatically for this test, stop the motor, and disarm before continuing. The fixed-wing motor should rotate counter-clockwise when viewed from tail to nose.")
                vehicle:    globals.activeVehicle
                testId:     0
            }
        }

        PreFlightCheckGroup {
            name: qsTr("Lift Motor Check")

            PreFlightCommandCheckButton {
                name:       qsTr("21. Test motor A")
                manualText: qsTr("Front-right motor should rotate counter-clockwise.")
                vehicle:    globals.activeVehicle
                testId:     1
            }
            PreFlightCommandCheckButton {
                name:       qsTr("22. Test motor B")
                manualText: qsTr("Rear-right motor should rotate clockwise.")
                vehicle:    globals.activeVehicle
                testId:     2
            }
            PreFlightCommandCheckButton {
                name:       qsTr("23. Test motor C")
                manualText: qsTr("Rear-left motor should rotate counter-clockwise.")
                vehicle:    globals.activeVehicle
                testId:     3
            }
            PreFlightCommandCheckButton {
                name:       qsTr("24. Test motor D")
                manualText: qsTr("Front-left motor should rotate clockwise.")
                vehicle:    globals.activeVehicle
                testId:     4
            }
        }

        PreFlightCheckGroup {
            name: qsTr("Flight Check Complete")

            PreFlightManualCheckButton {
                name:       qsTr("25. Aircraft flight check complete")
                manualText: qsTr("Confirm that all flight-check items have been completed.")
            }
        }
    }
}
