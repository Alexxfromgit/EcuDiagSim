import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

Panel {
    id: root
    title: "Driver"
    subtitle: "what you would do behind the wheel"

    readonly property var v: App.vehicle

    component Caption: Text {
        color: Theme.textDim
        font { pixelSize: 11; bold: true }
        Layout.preferredWidth: 74
    }

    // Scrolls when the window is too short for all controls (small laptop screens). Each row
    // is capped at the column width, otherwise rows keep their natural width and run off the side.
    Flickable {
        id: scroller
        readonly property bool overflows: contentHeight > height
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: scroller.overflows ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }

    ColumnLayout {
        id: column
        width: scroller.width - (scroller.overflows ? 14 : 0) // room for the scroll bar
        spacing: 10

        // --- ignition and engine ---
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Ignition" }
            Switch {
                checked: App.ignition === 1
                text: checked ? "On" : "Off"
                onToggled: App.ignition = checked ? 1 : 0
            }
            Button {
                text: root.v.cranking ? "Cranking…" : "Start engine"
                enabled: !root.v.engineRunning
                highlighted: App.ignition === 1 && !root.v.engineRunning
                onClicked: App.startEngine()
                ToolTip.visible: hovered
                ToolTip.text: "Needs the selector in P or N"
            }
            Chip {
                text: root.v.engineRunning ? "ENGINE RUNNING" : (root.v.cranking ? "STARTER" : "ENGINE OFF")
                tone: root.v.engineRunning ? Theme.ok : (root.v.cranking ? Theme.warn : Theme.textDim)
            }
            Item { Layout.fillWidth: true }
        }

        // --- park brake and fuel ---
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Park brake" }
            Switch {
                checked: App.parkBrake
                text: checked ? "Applied" : "Released"
                onToggled: App.parkBrake = checked
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.v.fuel.toFixed(1) + " l"
                color: root.v.fuel < 7 ? Theme.warn : Theme.textDim
                font { family: Theme.mono; pixelSize: 11 }
                ToolTip.visible: fuelHover.hovered
                ToolTip.text: "Fuel really in the tank, and what the injectors deliver now: "
                    + root.v.fuelRate.toFixed(2) + " l/h"
                HoverHandler { id: fuelHover }
            }
            Button {
                text: "Refuel"
                enabled: !root.v.engineRunning
                onClicked: App.refuel()
                ToolTip.visible: hovered
                ToolTip.text: "Fill the tank to " + App.cluster.tankLitres + " l (engine off)"
            }
        }

        // --- selector ---
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Selector" }
            Repeater {
                model: ["P", "R", "N", "D"]
                Button {
                    required property int index
                    required property string modelData
                    text: modelData
                    implicitWidth: 44
                    checkable: true
                    checked: App.selector === index
                    highlighted: checked
                    onClicked: App.selector = index
                }
            }
            Text {
                // Shrinks first when the panel is narrow.
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                text: "true speed " + root.v.speed.toFixed(1) + " km/h"
                color: Theme.textDim
                font { family: Theme.mono; pixelSize: 11 }
            }
        }

        // --- pedals ---
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Accelerator" }
            Slider {
                id: throttle
                Layout.fillWidth: true
                from: 0; to: 100
                value: App.throttle
                onMoved: App.throttle = value
            }
            Text {
                text: App.throttle.toFixed(0) + " %"
                color: Theme.text
                font { family: Theme.mono; pixelSize: 12 }
                Layout.preferredWidth: 44
                horizontalAlignment: Text.AlignRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Brake" }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 100
                value: App.brake
                onMoved: App.brake = value
            }
            Text {
                text: App.brake.toFixed(0) + " %"
                color: Theme.text
                font { family: Theme.mono; pixelSize: 12 }
                Layout.preferredWidth: 44
                horizontalAlignment: Text.AlignRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 6
            Caption { text: ""; Layout.alignment: Qt.AlignTop }
            // Wraps onto a second line when the panel is narrow instead of running off the side.
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Button { text: "Coast"; onClicked: { App.throttle = 0; App.brake = 0 } }
                Button { text: "Cruise 30 %"; onClicked: { App.brake = 0; App.throttle = 30 } }
                Button { text: "Full throttle"; onClicked: { App.brake = 0; App.throttle = 100 } }
                Button { text: "Stop"; onClicked: { App.throttle = 0; App.brake = 60 } }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // --- lights ---
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Lights" }
            Repeater {
                model: ["Off", "Park", "Low"]
                Button {
                    required property int index
                    required property string modelData
                    text: modelData
                    implicitWidth: 56
                    checkable: true
                    checked: App.headlamps === index
                    highlighted: checked
                    onClicked: App.headlamps = index
                }
            }
            Button {
                text: "High"
                implicitWidth: 56
                checkable: true
                checked: App.highBeam
                highlighted: checked
                onClicked: App.highBeam = !App.highBeam
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Signals" }
            Button {
                text: "◀ Left"
                checkable: true
                checked: App.turnLeft
                highlighted: checked
                onClicked: App.turnLeft = !App.turnLeft
            }
            Button {
                text: "Hazard"
                checkable: true
                checked: App.hazard
                highlighted: checked
                onClicked: App.hazard = !App.hazard
            }
            Button {
                text: "Right ▶"
                checkable: true
                checked: App.turnRight
                highlighted: checked
                onClicked: App.turnRight = !App.turnRight
            }
            Item { Layout.fillWidth: true }
        }

        // --- doors ---
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "Doors open"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
            Flow {
                Layout.fillWidth: true
                spacing: 2
                Repeater {
                    model: ["FL", "FR", "RL", "RR"]
                    CheckBox {
                        required property int index
                        required property string modelData
                        text: modelData
                        checked: App.doors[index]
                        onToggled: App.setDoor(index, checked)
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: ["Front left", "Front right", "Rear left", "Rear right"][index] + " door ajar"
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: column.width
            spacing: 8
            Caption { text: "" }
            Switch {
                text: "Central locking"
                checked: App.doorLock
                onToggled: App.doorLock = checked
            }
            Chip {
                text: root.v.locked ? "LOCKED" : "UNLOCKED"
                tone: root.v.locked ? Theme.accent : Theme.textDim
            }
            Item { Layout.fillWidth: true }
        }

    }
    }
}
