import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import EcuDiagSim

Panel {
    id: root

    readonly property string state_: App.scenarioState
    readonly property var levels: ["All", "Beginner", "Intermediate", "Advanced"]
    property string levelFilter: "All"
    readonly property var shown: {
        const list = []
        const all = App.scenarios
        for (let i = 0; i < all.length; ++i)
            if (levelFilter === "All" || all[i].level === levelFilter)
                list.push(Object.assign({ number: i + 1 }, all[i]))
        return list
    }

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
    function difficultyColor(level) {
        switch (level) {
        case "Beginner": return Theme.ok
        case "Intermediate": return Theme.warn
        case "Advanced": return Theme.violet
        default: return Theme.textDim
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
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.levels
                Button {
                    required property string modelData
                    text: modelData
                    highlighted: root.levelFilter === modelData
                    flat: !highlighted
                    onClicked: root.levelFilter = modelData
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.shown.length + " of " + App.scenarios.length
                color: Theme.textDim
                font.pixelSize: 11
            }
        }

        // --- lesson list: scrolls, shares the height with the log below ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 100
            Layout.minimumHeight: 90
            Layout.verticalStretchFactor: 3
            color: "transparent"
            ListView {
                id: lessons
                anchors.fill: parent
                clip: true
                spacing: 6
                model: root.shown
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: lessons.contentHeight > lessons.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property bool current: App.scenarioName === modelData.name
                    width: ListView.view.width - (lessons.contentHeight > lessons.height ? 12 : 0)
                    implicitHeight: cardLayout.implicitHeight + 16
                    height: implicitHeight
                    radius: 5
                    color: cardArea.containsMouse ? Theme.panelAlt : Theme.inset
                    border.color: current ? Theme.accent : Theme.border
                    border.width: current ? 2 : 1
                    Behavior on color { ColorAnimation { duration: 100 } }
                    MouseArea { id: cardArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    RowLayout {
                        id: cardLayout
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                        spacing: 10
                        Text {
                            text: card.modelData.number
                            color: card.current ? Theme.accent : Theme.off
                            font { pixelSize: 24; bold: true }
                            horizontalAlignment: Text.AlignHCenter
                            Layout.preferredWidth: 30
                            Layout.alignment: Qt.AlignTop
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: card.modelData.name
                                color: Theme.text
                                font { pixelSize: 13; bold: true }
                                elide: Text.ElideRight
                            }
                            Text {
                                id: desc
                                Layout.fillWidth: true
                                text: card.modelData.description
                                color: Theme.textDim
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                Chip {
                                    visible: (card.modelData.level ?? "") !== ""
                                    text: (card.modelData.level ?? "").toUpperCase()
                                    tone: root.difficultyColor(card.modelData.level)
                                }
                                Repeater {
                                    model: card.modelData.topics ?? []
                                    Chip {
                                        required property string modelData
                                        text: modelData
                                        tone: Theme.textDim
                                    }
                                }
                                // Optional equipment the lesson needs; it is fitted for the run if missing.
                                Repeater {
                                    model: card.modelData.equipment ?? []
                                    Chip {
                                        required property var modelData
                                        readonly property bool owned: App.equipment.indexOf(modelData.id) >= 0
                                        text: "⚙ " + modelData.name + (owned ? "" : "  · fitted for this lesson")
                                        tone: Theme.accent
                                        ToolTip.visible: chipHover.hovered
                                        ToolTip.text: owned ? "Fitted to your vehicle (Settings)"
                                                            : "Not in your vehicle: running the lesson fits it for this run only"
                                        HoverHandler { id: chipHover }
                                    }
                                }
                            }
                        }
                        Button {
                            Layout.alignment: Qt.AlignVCenter
                            text: card.current && root.state_ === "running" ? "Running…" : "Run"
                            highlighted: true
                            enabled: root.state_ !== "running"
                            onClicked: App.runScenario(card.modelData.path)
                        }
                    }
                    ToolTip.visible: cardArea.containsMouse && desc.truncated
                    ToolTip.delay: 600
                    ToolTip.text: card.modelData.description
                }
                Text {
                    anchors.centerIn: parent
                    visible: lessons.count === 0
                    text: "No lessons at this level."
                    color: Theme.textDim
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
            Layout.preferredHeight: 100
            Layout.minimumHeight: 110
            Layout.verticalStretchFactor: 2
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
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: logView.count === 0
                text: "Pick a lesson above. Running one resets the vehicle first."
                color: Theme.textDim
            }
        }
    }
}
