import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

// The instrument cluster (IPC): everything shown here arrived over CAN.
Panel {
    id: root
    title: "Instrument cluster"
    subtitle: c.powered ? "shows what the IPC receives on the bus" : "ignition off"

    readonly property var c: App.cluster
    readonly property bool powered: c.powered
    // Ignition on: both needles sweep while the cluster runs its bulb check.
    onPoweredChanged: {
        if (powered && !App.reducedMotion && !App.replaying) {
            rpmGauge.selfTest(App.speedFactor)
            speedGauge.selfTest(App.speedFactor)
        }
    }
    // Height at which the two gauges fill their width exactly (no empty bands above and below).
    readonly property real gaugeSize: Math.max(120, (width - 20 - 92 - 8) / 2)
    readonly property real idealHeight: 36 + gaugeSize + 6 + lamps.Layout.preferredHeight + 2 + 14

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Gauge {
                id: rpmGauge
                Layout.fillWidth: true
                Layout.fillHeight: true
                value: root.c.rpm
                maximum: 7000
                majorStep: 1000
                labelDivisor: 1000
                redFrom: 6200
                unit: "rpm"
                caption: "×1000 rpm"
                lit: root.c.powered
            }

            ColumnLayout {
                Layout.preferredWidth: 92
                Layout.fillHeight: true
                spacing: 4
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 76; height: 46
                    radius: 6
                    color: "#0c0e12"
                    border.color: "#343b48"
                    Text {
                        anchors.centerIn: parent
                        text: root.c.gear === "" ? "–" : root.c.gear
                        color: root.c.powered ? Theme.accent : Theme.off
                        font { family: Theme.mono; pixelSize: 30; bold: true }
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "GEAR"
                    color: Theme.textDim
                    font.pixelSize: 9
                }
                // coolant bar 40..130 degC
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 76; height: 8; radius: 4
                    color: "#0c0e12"
                    border.color: "#343b48"
                    Rectangle {
                        visible: root.c.powered
                        height: parent.height; radius: 4
                        width: Math.max(4, Math.min(1, (root.c.coolant - 40) / 90) * parent.width)
                        color: root.c.coolant > 112 ? Theme.error : (root.c.coolant < 60 ? Theme.accent : Theme.ok)
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.c.powered ? root.c.coolant.toFixed(0) + " °C   " + root.c.voltage.toFixed(1) + " V" : " "
                    color: Theme.textDim
                    font { family: Theme.mono; pixelSize: 10 }
                }
                // fuel gauge: as the ECM reports the tank (a faulty sender drops it to empty)
                Rectangle {
                    id: fuelBar
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 2
                    width: 76; height: 8; radius: 4
                    color: "#0c0e12"
                    border.color: "#343b48"
                    readonly property real fraction: Math.max(0, Math.min(1, root.c.fuelLevel / root.c.tankLitres))
                    Rectangle {
                        visible: root.c.powered
                        height: parent.height; radius: 4
                        width: Math.max(4, fuelBar.fraction * parent.width)
                        color: root.c.lowFuel ? Theme.warn : Theme.accent
                        Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                    }
                    // quarter marks
                    Repeater {
                        model: 3
                        Rectangle {
                            required property int index
                            x: (index + 1) * fuelBar.width / 4; y: 2
                            width: 1; height: fuelBar.height - 4
                            color: "#0c0e12"
                            opacity: 0.8
                        }
                    }
                }
                // trip computer
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: !root.c.powered ? " "
                          : !root.c.fuelValid ? "FUEL  --"
                          : root.c.fuelLevel.toFixed(0) + " l  " + root.c.range.toFixed(0) + " km"
                    color: Theme.textDim
                    font { family: Theme.mono; pixelSize: 10 }
                    HoverHandler { id: tripHover; cursorShape: root.c.powered ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    // Like the trip button on a real stalk: a click starts a new trip (average and range).
                    TapHandler { enabled: root.c.powered; onTapped: App.resetTrip() }
                    ToolTip.visible: tripHover.hovered && root.c.powered
                    ToolTip.text: "Trip computer (IPC, from ECM_Fuel)\n"
                        + "tank " + root.c.fuelLevel.toFixed(1) + " l, range " + root.c.range.toFixed(0) + " km\n"
                        + "fuel rate " + root.c.fuelRate.toFixed(2) + " l/h\n"
                        + "average " + (root.c.averageConsumption < 0 ? "--" : root.c.averageConsumption.toFixed(1) + " l/100 km")
                        + "\n\nClick to reset the trip"
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: !root.c.powered ? " "
                          : root.c.consumption >= 0 ? root.c.consumption.toFixed(1) + " l/100km"
                          : root.c.fuelRate.toFixed(1) + " l/h"
                    color: Theme.off
                    font { family: Theme.mono; pixelSize: 9 }
                }
                Item { Layout.fillHeight: true }
            }

            Gauge {
                id: speedGauge
                Layout.fillWidth: true
                Layout.fillHeight: true
                value: root.c.speed
                maximum: 240
                majorStep: 20
                unit: "km/h"
                lit: root.c.powered
            }
        }

        // Tell-tales between the turn arrows. They wrap into balanced rows as systems are fitted, so
        // labels never shrink below legibility; a new system only needs an entry in `telltales`.
        Item {
            id: lamps
            Layout.fillWidth: true
            Layout.preferredHeight: rows * lampHeight + (rows - 1) * gap
            readonly property real gap: rows > 1 ? 3 : 4
            // Wrapped rows are a little shorter so the cluster grows as little as possible.
            readonly property real lampHeight: rows > 1 ? 21 : 24
            readonly property real minCell: 40
            readonly property var telltales: [
                { key: "airbag", label: "AIRBAG", tone: Theme.error, fitted: "srs",
                  tip: "Airbag system (SRS_Status.AirbagLampReq, own 6 s lamp check, or the module silent)" },
                { key: "seatBelt", label: "BELT", tone: Theme.error, fitted: "srs",
                  tip: "Seat belt reminder (SRS_Status.BeltWarn; flashes above 20 km/h)" },
                { key: "parkBrake", label: "BRAKE", tone: Theme.error,
                  tip: "Park brake applied (BCM_Status.ParkBrake); flashes when driving with it (ParkBrakeWarn)" },
                { key: "battery", label: "BATT", tone: Theme.error, tip: "Charge warning: engine not running or battery below 12 V" },
                { key: "coolantWarn", label: "TEMP", tone: Theme.error, tip: "Coolant temperature (ECM_Status.CoolantWarnReq)" },
                { key: "oilWarn", label: "OIL", tone: Theme.error, tip: "Oil pressure (ECM_Fuel.OilPressureWarn, or ECM silent)" },
                { key: "doorAjar", label: "DOOR", tone: Theme.error, tip: "Door ajar (BCM_Status.DoorAjar_xx)" },
                { key: "mil", label: "CHECK", tip: "Malfunction indicator lamp (ECM_Status.MilReq, or ECM silent)" },
                { key: "abs", label: "ABS", tip: "ABS warning (ABS_Status.AbsLampReq, or ABS silent)" },
                { key: "esc", label: "ESC", fitted: "esc",
                  tip: "Stability control: flashing while it intervenes (ABS_Chassis.EscActive), steady when it is off (EscOff)" },
                { key: "tcs", label: "TCS", tip: "Traction control off (ABS_Status.TcsLampReq)" },
                { key: "tpms", label: "TPMS", fitted: "tpms",
                  tip: "Tyre pressure: steady = a tyre 25 % below the placard; flashing for a minute, then steady = system fault (BCM_Tpms.TpmsLampReq)" },
                { key: "transFault", label: "TRANS", tip: "Transmission limp mode (TCM_Status.LimpMode, or TCM silent)" },
                { key: "lowFuel", label: "FUEL", tip: "Low fuel: below 7 l, or no valid level from the fuel sender (ECM_Fuel)" },
                { key: "lampFault", label: "BULB", tip: "Exterior lamp failure (BCM_Status.LampFault)" },
                { key: "lowBeam", label: "LOW", tone: Theme.ok, tip: "Low beam (BCM_Status.Headlamps)" },
                { key: "highBeam", label: "HIGH", tone: Theme.accent, tip: "High beam (BCM_Status.HighBeam)" }
            ]
            readonly property var shown: telltales.filter(lamp => !lamp.fitted || App.fitted.indexOf(lamp.fitted) >= 0)
            readonly property real area: Math.max(minCell, width - 2 * (turnLeft.width + 8))
            readonly property int maxColumns: Math.max(1, Math.floor((area + gap) / (minCell + gap)))
            readonly property int rows: Math.max(1, Math.ceil(shown.length / maxColumns))
            readonly property int columns: Math.max(1, Math.ceil(shown.length / rows))
            readonly property real cell: Math.min(58, Math.floor((area - (columns - 1) * gap) / columns))

            Lamp {
                id: turnLeft
                objectName: "telltale:turnLeft"
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: 24; height: 24
                on: root.c.turnLeft; label: "◀"; tone: Theme.ok; tip: "Left turn signal (BCM_Status.TurnLeft)"
            }
            Grid {
                anchors.centerIn: parent
                columns: lamps.columns
                spacing: lamps.gap
                Repeater {
                    model: lamps.shown
                    Lamp {
                        required property var modelData
                        objectName: "telltale:" + modelData.key
                        width: lamps.cell; height: lamps.lampHeight
                        haloSpread: Math.floor(lamps.gap / 2)
                        on: root.c[modelData.key] === true
                        label: modelData.label
                        tone: modelData.tone ?? Theme.warn
                        tip: modelData.tip
                    }
                }
            }
            Lamp {
                objectName: "telltale:turnRight"
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                width: 24; height: 24
                on: root.c.turnRight; label: "▶"; tone: Theme.ok; tip: "Right turn signal (BCM_Status.TurnRight)"
            }
        }
    }
}
