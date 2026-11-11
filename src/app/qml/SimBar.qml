import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

Rectangle {
    id: root
    implicitHeight: 46
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radius

    readonly property var speeds: [0.25, 0.5, 1, 2, 5, 10]
    // Below this width (1366x768 laptops) the less important items give way.
    readonly property bool compact: width < 1450

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        Text {
            visible: !root.compact
            text: "ECU DIAGNOSTICS SIMULATOR"
            color: Theme.text
            font { pixelSize: 13; bold: true; letterSpacing: 1.5 }
        }
        Rectangle { visible: !root.compact; width: 1; Layout.fillHeight: true; Layout.margins: 10; color: Theme.border }

        Button {
            text: App.running ? "❚❚  Pause" : "▶  Run"
            highlighted: !App.running
            implicitWidth: 96
            onClicked: App.running = !App.running
        }
        Button {
            text: "Step 10 ms"
            onClicked: App.step(10)
            ToolTip.visible: hovered
            ToolTip.text: "Pause and advance one ABS/ECM cycle"
        }
        Button { visible: !root.compact; text: "Step 100 ms"; onClicked: App.step(100) }

        Text { text: "Speed"; color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            implicitWidth: 86
            model: root.speeds.map(s => s + "×")
            currentIndex: {
                let best = -1
                for (let i = 0; i < root.speeds.length; ++i)
                    if (Math.abs(root.speeds[i] - App.speedFactor) < 0.01)
                        best = i
                return best
            }
            // Also shows factors set from the command line that are not in the list (e.g. --speed 4).
            displayText: App.speedFactor + "×"
            onActivated: index => App.speedFactor = root.speeds[index]
        }

        Rectangle { width: 1; Layout.fillHeight: true; Layout.margins: 10; color: Theme.border }

        Text { visible: !root.compact; text: "Sim time"; color: Theme.textDim; font.pixelSize: 12 }
        Text {
            text: App.simTime.toFixed(2) + " s"
            color: Theme.text
            font { family: Theme.mono; pixelSize: 14 }
            Layout.preferredWidth: 80
        }

        Text { text: "Bus load"; color: Theme.textDim; font.pixelSize: 12 }
        Rectangle {
            Layout.preferredWidth: 90
            height: 10
            radius: 5
            color: Theme.inset
            border.color: Theme.border
            Rectangle {
                width: Math.max(0, Math.min(1, App.busLoad)) * parent.width
                height: parent.height
                radius: 5
                color: App.busLoad > 0.7 ? Theme.error : (App.busLoad > 0.4 ? Theme.warn : Theme.ok)
            }
        }
        // Takes the free space and gives it up first when the window is narrow.
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 50
            text: (App.busLoad * 100).toFixed(1) + " %   " + App.frameRate.toFixed(0) + " frames/s   500 kbit/s"
            color: Theme.textDim
            font { family: Theme.mono; pixelSize: 12 }
            elide: Text.ElideRight
        }

        Chip {
            visible: App.replaying
            text: "REPLAY " + (App.replayProgress * 100).toFixed(0) + " %"
            tone: Theme.violet
        }
        Button { visible: App.replaying; text: "Stop replay"; onClicked: App.stopReplay() }
        Chip {
            visible: App.scenarioState === "running"
            text: "LESSON RUNNING"
            tone: Theme.accent
        }
        Button {
            text: "Reset"
            onClicked: App.reset()
            ToolTip.visible: hovered
            ToolTip.text: "New vehicle: clears faults, fault memories and the trace"
        }
    }
}
