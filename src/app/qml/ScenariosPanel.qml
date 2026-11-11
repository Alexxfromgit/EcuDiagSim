import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import EcuDiagSim

Panel {
    id: root

    readonly property string state_: App.scenarioState

    FileDialog {
        id: openDialog
        title: "Run a scenario file"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Scenario (*.json)"]
        onAccepted: App.runScenarioFile(selectedFile)
    }

    function levelColor(level) {
        switch (level) {
        case "pass": return Theme.ok
        case "fail": return Theme.error
        case "action": return Theme.accent
        default: return Theme.textDim
        }
    }
    function levelMark(level) {
        switch (level) {
        case "pass": return "✔"
        case "fail": return "✘"
        case "action": return "▸"
        default: return "·"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: "Guided lessons: each one drives the car, injects a fault and checks every link of the chain — sensor, CAN frame, DTC, lamp, scan tool. "
                  + "Watch the other panels while it runs, or slow the simulation down."
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: App.scenarios
            Rectangle {
                id: card
                required property var modelData
                required property int index
                readonly property bool current: App.scenarioName === modelData.name
                Layout.fillWidth: true
                implicitHeight: cardLayout.implicitHeight + 16
                radius: 4
                color: Theme.inset
                border.color: current ? Theme.accent : Theme.border
                RowLayout {
                    id: cardLayout
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                    spacing: 10
                    Text {
                        text: card.index + 1
                        color: Theme.off
                        font { pixelSize: 26; bold: true }
                        Layout.preferredWidth: 22
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: card.modelData.name
                            color: Theme.text
                            font { pixelSize: 13; bold: true }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: card.modelData.description
                            color: Theme.textDim
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                    Button {
                        text: "Run"
                        highlighted: true
                        enabled: root.state_ !== "running"
                        onClicked: App.runScenario(card.modelData.path)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button { text: "Run a file…"; onClicked: openDialog.open() }
            Button { text: "Stop"; enabled: root.state_ === "running"; onClicked: App.stopScenario() }
            ProgressBar {
                Layout.fillWidth: true
                value: App.scenarioProgress
                visible: root.state_ !== "idle"
            }
            Item { Layout.fillWidth: true; visible: root.state_ === "idle" }
            Chip {
                visible: root.state_ !== "idle"
                filled: root.state_ !== "running"
                text: root.state_.toUpperCase()
                tone: root.state_ === "passed" ? Theme.ok : (root.state_ === "failed" ? Theme.error : Theme.accent)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.inset
            border.color: Theme.border
            radius: 4
            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                spacing: 2
                model: App.scenarioLog
                onCountChanged: Qt.callLater(positionViewAtEnd)
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    id: entry
                    required property var modelData
                    width: ListView.view.width - 10
                    spacing: 8
                    Text {
                        text: entry.modelData.time + " s"
                        color: Theme.off
                        font { family: Theme.mono; pixelSize: 11 }
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 62
                        Layout.alignment: Qt.AlignTop
                    }
                    Text {
                        text: root.levelMark(entry.modelData.level)
                        color: root.levelColor(entry.modelData.level)
                        font { pixelSize: 12; bold: true }
                        Layout.preferredWidth: 12
                        Layout.alignment: Qt.AlignTop
                    }
                    Text {
                        Layout.fillWidth: true
                        text: entry.modelData.text
                        color: entry.modelData.level === "info" ? Theme.text : root.levelColor(entry.modelData.level)
                        font { pixelSize: 12; bold: entry.modelData.level === "info" }
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: logView.count === 0
                text: "Pick a lesson above. Running one resets the vehicle first."
                color: Theme.textDim
            }
        }
    }
}
