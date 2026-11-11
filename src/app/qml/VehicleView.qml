import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

// Top view of the car: control units on the CAN bus, sensors and components as hotspots.
Panel {
    id: root
    title: "Vehicle"
    subtitle: "click a part to inject a fault, a control unit to diagnose it"

    signal targetPicked(string id)
    signal ecuPicked(int index)

    readonly property var v: App.vehicle
    readonly property var sensorMap: {
        const m = {}
        for (const s of App.sensors)
            m[s.id] = s
        return m
    }
    readonly property var ecuMap: {
        const m = {}
        for (const e of App.ecus)
            m[e.name] = e
        return m
    }
    readonly property var shortNames: ({
        "wss_fl": "Wheel FL", "wss_fr": "Wheel FR", "wss_rl": "Wheel RL", "wss_rr": "Wheel RR",
        "ckp": "Crankshaft", "ect": "Coolant", "tps": "Throttle", "tft": "Trans. fluid"
    })
    readonly property var faultedIds: App.activeFaults.map(f => f.targetId)

    // --- building blocks ---------------------------------------------------
    component EcuBox: Rectangle {
        id: box
        property string name: ""
        property int diagIndex: -1
        readonly property var info: root.ecuMap[name] ?? ({ state: "sleep", dtcCount: 0, description: "" })
        readonly property color tone: Theme.ecuColor(info.state)

        radius: 5
        color: info.state === "nopower" ? "#17191d" : Qt.rgba(tone.r, tone.g, tone.b, 0.13)
        border.color: tone
        border.width: area.containsMouse ? 2 : 1

        Text {
            anchors.centerIn: parent
            text: box.name
            color: box.info.state === "sleep" || box.info.state === "nopower" ? Theme.textDim : Theme.text
            font { pixelSize: 12; bold: true }
        }
        Rectangle {
            visible: box.info.dtcCount > 0
            anchors { right: parent.right; top: parent.top; margins: -7 }
            width: 18; height: 18; radius: 9
            color: Theme.warn
            Text {
                anchors.centerIn: parent
                text: box.info.dtcCount
                color: "#0b0d10"
                font { pixelSize: 10; bold: true }
            }
        }
        Text {
            visible: box.info.state === "nopower" || box.info.state === "busoff"
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.bottom; topMargin: 1 }
            text: box.info.state === "nopower" ? "NO POWER" : "BUS-OFF"
            color: box.info.state === "nopower" ? Theme.error : Theme.violet
            font { pixelSize: 8; bold: true }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton || box.diagIndex < 0)
                    root.targetPicked(box.name.toLowerCase())
                else
                    root.ecuPicked(box.diagIndex)
            }
        }
        ToolTip.visible: area.containsMouse
        ToolTip.delay: 300
        ToolTip.text: box.info.description + "\n" + Theme.ecuStateText(box.info.state)
            + (box.info.dtcCount > 0 ? "\n" + box.info.dtcCount + " DTC(s) stored" : "")
            + (box.diagIndex >= 0 ? "\n\nClick: open in scan tool\nRight click: power / bus faults" : "\n\nClick: power / bus faults")
    }

    component Hotspot: Item {
        id: spot
        property string targetId: ""
        property string label: ""
        property bool labelLeft: false
        readonly property var sensor: root.sensorMap[targetId]
        readonly property bool faulted: root.faultedIds.indexOf(targetId) >= 0

        width: 14; height: 14
        Rectangle {
            anchors.fill: parent
            radius: 7
            color: spot.faulted ? Theme.error : "#1d2f29"
            border.color: spot.faulted ? "#ffb3ba" : Theme.ok
            border.width: spotArea.containsMouse ? 2 : 1
            SequentialAnimation on opacity {
                running: spot.faulted
                loops: Animation.Infinite
                alwaysRunToEnd: true
                NumberAnimation { to: 0.35; duration: 450 }
                NumberAnimation { to: 1.0; duration: 450 }
            }
        }
        Text {
            text: spot.label
            color: spot.faulted ? Theme.error : Theme.textDim
            font { pixelSize: 9; bold: true }
            anchors.verticalCenter: parent.verticalCenter
            x: spot.labelLeft ? -implicitWidth - 4 : parent.width + 4
        }
        MouseArea {
            id: spotArea
            anchors.fill: parent
            anchors.margins: -5
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.targetPicked(spot.targetId)
        }
        ToolTip.visible: spotArea.containsMouse
        ToolTip.delay: 200
        ToolTip.text: {
            const s = spot.sensor
            if (!s)
                return spot.label + (spot.faulted ? "\nFAULT ACTIVE" : "") + "\n\nClick to inject a fault"
            let t = s.name + "  →  " + s.ecu
                + "\nreal value:     " + s.trueValue.toFixed(1) + " " + s.unit
                + "\nECU measures:   " + s.value.toFixed(1) + " " + s.unit
            if (!s.digital)
                t += "\npin voltage:    " + s.voltage.toFixed(2) + " V"
            t += "\ncircuit:        " + s.circuit
            if (s.faulted)
                t += "\nFAULT:          " + s.fault
            return t + "\n\nClick to inject a fault"
        }
    }

    component Wire: Rectangle {
        color: root.v.busFailure ? Theme.error : "#c9a227"
        opacity: root.v.busFailure ? 0.9 : 0.75
    }

    component Wheel: Rectangle {
        width: 26; height: 66; radius: 6
        color: "#0c0e12"
        border.color: "#4a5262"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 240
            Layout.maximumHeight: 470
            Layout.alignment: Qt.AlignHCenter

            Item {
                id: car
                width: 380; height: 470
                anchors.centerIn: parent
                // Shrinks on small screens (e.g. 1366x768) so the sensor table below stays visible.
                scale: Math.min(1, stage.width / width, stage.height / height)

                Text { x: 168; y: 0; text: "FRONT"; color: Theme.off; font { pixelSize: 9; bold: true } }

                Wheel { x: 72; y: 70 }
                Wheel { x: 282; y: 70 }
                Wheel { x: 72; y: 340 }
                Wheel { x: 282; y: 340 }

                // body
                Rectangle {
                    x: 96; y: 16; width: 188; height: 444
                    radius: 46
                    color: "#161a21"
                    border.color: "#4a5262"
                    border.width: 2
                }
                // windscreen, cabin, rear window
                Rectangle { x: 116; y: 176; width: 148; height: 18; radius: 8; color: "#1f2733" }
                Rectangle { x: 112; y: 198; width: 156; height: 150; radius: 14; color: "#11141a"; border.color: "#2a303b" }
                Rectangle { x: 120; y: 352; width: 140; height: 14; radius: 7; color: "#1f2733" }
                // headlamps
                Rectangle {
                    x: 110; y: 20; width: 34; height: 10; radius: 5
                    color: root.v.lowBeam && root.faultedIds.indexOf("bulb_low_left") < 0 ? "#ffe9a3" : "#2a303b"
                }
                Rectangle {
                    x: 236; y: 20; width: 34; height: 10; radius: 5
                    color: root.v.lowBeam ? "#ffe9a3" : "#2a303b"
                }

                // --- CAN bus: backbone with stubs ---
                Wire { x: 189; y: 96; width: 2; height: 204 }      // backbone
                Wire { x: 172; y: 139; width: 18; height: 2 }      // ABS
                Wire { x: 190; y: 163; width: 18; height: 2 }      // TCM
                Wire { x: 136; y: 262; width: 54; height: 2 }      // DLC
                Text {
                    x: 194; y: 106
                    text: root.v.busFailure ? "CAN  ✕" : "CAN 500k"
                    color: root.v.busFailure ? Theme.error : "#c9a227"
                    font { pixelSize: 8; bold: true }
                }
                MouseArea {
                    x: 182; y: 96; width: 16; height: 30
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.targetPicked("can_bus")
                }

                // --- control units ---
                EcuBox { name: "ECM"; diagIndex: 0; x: 144; y: 58; width: 92; height: 38 }
                EcuBox { name: "ABS"; diagIndex: 2; x: 112; y: 124; width: 60; height: 32 }
                EcuBox { name: "TCM"; diagIndex: 1; x: 208; y: 148; width: 60; height: 32 }
                EcuBox { name: "IPC"; x: 146; y: 206; width: 88; height: 28 }
                EcuBox { name: "BCM"; diagIndex: 3; x: 152; y: 300; width: 76; height: 34 }

                // OBD connector (scan tool)
                Rectangle {
                    x: 106; y: 252; width: 30; height: 22; radius: 3
                    color: "#22303f"; border.color: Theme.accent
                    Text { anchors.centerIn: parent; text: "OBD"; color: Theme.accent; font { pixelSize: 8; bold: true } }
                }

                // --- sensors and components ---
                Hotspot { targetId: "wss_fl"; label: "WSS"; x: 102; y: 96; labelLeft: true }
                Hotspot { targetId: "wss_fr"; label: "WSS"; x: 264; y: 96 }
                Hotspot { targetId: "wss_rl"; label: "WSS"; x: 102; y: 366; labelLeft: true }
                Hotspot { targetId: "wss_rr"; label: "WSS"; x: 264; y: 366 }
                Hotspot { targetId: "ckp"; label: "CKP"; x: 150; y: 38; labelLeft: true }
                Hotspot { targetId: "ect"; label: "ECT"; x: 183; y: 36 }
                Hotspot { targetId: "tps"; label: "TPS"; x: 222; y: 38 }
                Hotspot { targetId: "tft"; label: "TFT"; x: 246; y: 186 }
                Hotspot { targetId: "alternator"; label: "ALT"; x: 118; y: 66; labelLeft: true }
                Hotspot { targetId: "cooling"; label: "PUMP"; x: 246; y: 66 }
                Hotspot { targetId: "bulb_low_left"; label: "BULB"; x: 100; y: 36; labelLeft: true }
                Hotspot { targetId: "abs_e2e"; label: "E2E"; x: 120; y: 160; labelLeft: true }
            }
        }

        // --- what the sensors really see vs. what the ECU measures ---
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "SENSOR"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true }
                Text { text: "REAL"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 46 }
                Text { text: "ECU MEASURES"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 78 }
                Text { text: "PIN"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 46 }
                Text { text: "STATE"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 84 }
            }
            Repeater {
                model: ["wss_fl", "wss_fr", "wss_rl", "wss_rr", "ckp", "ect", "tps", "tft"]
                Item {
                    id: cell
                    required property string modelData
                    readonly property var s: root.sensorMap[modelData]
                    Layout.fillWidth: true
                    implicitHeight: 15
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        Text {
                            text: root.shortNames[cell.modelData] ?? ""
                            color: cell.s && cell.s.faulted ? Theme.error : Theme.text
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: cell.s ? cell.s.trueValue.toFixed(1) : ""
                            color: Theme.textDim
                            font { family: Theme.mono; pixelSize: 11 }
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 46
                        }
                        Text {
                            text: cell.s ? cell.s.value.toFixed(1) + " " + cell.s.unit : ""
                            color: cell.s && Math.abs(cell.s.value - cell.s.trueValue) > 0.6 ? Theme.error : Theme.text
                            font { family: Theme.mono; pixelSize: 11 }
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 78
                        }
                        Text {
                            text: cell.s && !cell.s.digital ? cell.s.voltage.toFixed(2) + " V" : "pulse"
                            color: Theme.textDim
                            font { family: Theme.mono; pixelSize: 11 }
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 46
                        }
                        Text {
                            text: cell.s ? (cell.s.faulted ? cell.s.fault : "ok") : ""
                            color: cell.s && cell.s.faulted ? Theme.error : Theme.ok
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.preferredWidth: 84
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.targetPicked(cell.modelData)
                    }
                }
            }
        }
        Item { Layout.fillHeight: true; Layout.preferredHeight: 0 } // spare height goes below the table
    }
}
