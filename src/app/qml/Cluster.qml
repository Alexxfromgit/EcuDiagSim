import QtQuick
import QtQuick.Layouts
import EcuDiagSim

// The instrument cluster (IPC): everything shown here arrived over CAN.
Panel {
    id: root
    title: "Instrument cluster"
    subtitle: c.powered ? "shows what the IPC receives on the bus" : "ignition off"

    readonly property var c: App.cluster

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Gauge {
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
                spacing: 6
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 76; height: 54
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
                Item { Layout.fillHeight: true }
            }

            Gauge {
                Layout.fillWidth: true
                Layout.fillHeight: true
                value: root.c.speed
                maximum: 240
                majorStep: 20
                unit: "km/h"
                lit: root.c.powered
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Lamp { on: root.c.turnLeft; label: "◀"; tone: Theme.ok; tip: "Left turn signal (BCM_Status.TurnLeft)" }
            Item { Layout.fillWidth: true }
            Lamp { on: root.c.mil; label: "CHECK"; tip: "Malfunction indicator lamp (ECM_Status.MilReq, or ECM silent)" }
            Lamp { on: root.c.abs; label: "ABS"; tip: "ABS warning (ABS_Status.AbsLampReq, or ABS silent)" }
            Lamp { on: root.c.tcs; label: "TCS"; tip: "Traction control off (ABS_Status.TcsLampReq)" }
            Lamp { on: root.c.battery; label: "BATT"; tone: Theme.error; tip: "Charge warning: engine not running or battery below 12 V" }
            Lamp { on: root.c.coolantWarn; label: "TEMP"; tone: Theme.error; tip: "Coolant temperature (ECM_Status.CoolantWarnReq)" }
            Lamp { on: root.c.transFault; label: "TRANS"; tip: "Transmission limp mode (TCM_Status.LimpMode, or TCM silent)" }
            Lamp { on: root.c.lampFault; label: "BULB"; tip: "Exterior lamp failure (BCM_Status.LampFault)" }
            Lamp { on: root.c.doorAjar; label: "DOOR"; tone: Theme.error; tip: "Door ajar (BCM_Status.DoorAjar_xx)" }
            Lamp { on: root.c.lowBeam; label: "LOW"; tone: Theme.ok; tip: "Low beam (BCM_Status.Headlamps)" }
            Lamp { on: root.c.highBeam; label: "HIGH"; tone: Theme.accent; tip: "High beam (BCM_Status.HighBeam)" }
            Item { Layout.fillWidth: true }
            Lamp { on: root.c.turnRight; label: "▶"; tone: Theme.ok; tip: "Right turn signal (BCM_Status.TurnRight)" }
        }
    }
}
