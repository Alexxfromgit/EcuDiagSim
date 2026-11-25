pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

// Settings: vehicle equipment, simulation start-up and display options.
// Equipment changes are collected and applied together, because they build a new vehicle.
Popup {
    id: root
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: Math.min(860, (parent ? parent.width : 860) - 60)
    height: Math.min(760, (parent ? parent.height : 760) - 60)

    // Equipment ids switched on in the page, not applied yet.
    property var pending: []
    property bool confirmStop: false
    readonly property bool dirty: JSON.stringify(pending.slice().sort()) !== JSON.stringify(App.equipment.slice().sort())
    readonly property bool lessonRunning: App.scenarioState === "running"

    onAboutToShow: {
        pending = App.equipment.slice()
        confirmStop = false
    }

    function toggle(id, on) {
        const list = pending.filter(x => x !== id)
        if (on)
            list.push(id)
        pending = list
        confirmStop = false
    }
    function apply() {
        if (lessonRunning && !confirmStop) {
            confirmStop = true
            return
        }
        App.setEquipment(pending)
        confirmStop = false
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0.02, 0.03, 0.04, 0.62)
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: App.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: App.reducedMotion ? 0 : 200; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: App.reducedMotion ? 0 : 120; easing.type: Easing.InCubic }
    }

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: 10
    }

    component SectionTitle: Text {
        color: Theme.textDim
        font { pixelSize: 11; bold: true; letterSpacing: 1.2 }
        Layout.topMargin: 6
    }

    component SettingRow: RowLayout {
        id: row
        property string label: ""
        property string hint: ""
        default property alias control: holder.data
        Layout.fillWidth: true
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: row.label; color: Theme.text; font.pixelSize: 13 }
            Text {
                Layout.fillWidth: true
                visible: row.hint !== ""
                text: row.hint
                color: Theme.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- header ---
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 10
            spacing: 10
            Text {
                text: "Settings"
                color: Theme.text
                font { pixelSize: 18; bold: true }
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "✕"
                onClicked: root.close()
                ToolTip.visible: hovered
                ToolTip.text: "Close (Esc)"
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: scroll.availableWidth - 32
                x: 16
                spacing: 10

                Item { implicitHeight: 6 }

                // --- vehicle equipment ---
                SectionTitle { text: "VEHICLE EQUIPMENT" }
                Text {
                    Layout.fillWidth: true
                    text: "Optional systems fitted to the car. Each one brings its own sensors, control unit functions, "
                          + "cluster lamps, fault targets and lessons. Applying a change builds a new vehicle: faults, "
                          + "fault memories and the trace start from scratch. A lesson fits what it needs by itself."
                    color: Theme.textDim
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: scroll.availableWidth > 700 ? 2 : 1
                    columnSpacing: 10
                    rowSpacing: 10
                    Repeater {
                        model: App.equipmentCatalog
                        Rectangle {
                            id: card
                            required property var modelData
                            readonly property bool on: root.pending.indexOf(modelData.id) >= 0
                            readonly property bool available: modelData.implemented
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.alignment: Qt.AlignTop
                            implicitHeight: cardColumn.implicitHeight + 24
                            radius: 8
                            color: on ? Qt.rgba(0.29, 0.66, 1, 0.08) : Theme.inset
                            border.color: on ? Theme.accent : (cardHover.hovered && available ? "#3c4452" : Theme.border)
                            opacity: available ? 1 : 0.62
                            Behavior on color { ColorAnimation { duration: 140 } }
                            Behavior on border.color { ColorAnimation { duration: 140 } }
                            HoverHandler { id: cardHover }

                            ColumnLayout {
                                id: cardColumn
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: card.modelData.name
                                        color: Theme.text
                                        font { pixelSize: 14; bold: true }
                                        elide: Text.ElideRight
                                    }
                                    Chip {
                                        visible: !card.available
                                        text: "IN DEVELOPMENT"
                                        tone: Theme.violet
                                    }
                                    Switch {
                                        enabled: card.available
                                        checked: card.on
                                        onToggled: root.toggle(card.modelData.id, checked)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: card.modelData.description
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Repeater {
                                        model: card.modelData.adds
                                        Chip {
                                            required property string modelData
                                            text: modelData
                                            tone: card.on ? Theme.accent : Theme.textDim
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: card.modelData.lessons.length > 0
                                    text: "Lessons: " + card.modelData.lessons.join(" · ")
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: root.confirmStop ? "A lesson is running. Applying stops it and resets the vehicle."
                            : root.dirty ? "Not applied yet." : "Fitted: " + (App.equipment.length ? App.equipment.join(", ") : "base vehicle only")
                        color: root.confirmStop ? Theme.warn : Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Button {
                        visible: root.dirty
                        text: "Discard"
                        flat: true
                        onClicked: { root.pending = App.equipment.slice(); root.confirmStop = false }
                    }
                    Button {
                        enabled: root.dirty
                        highlighted: true
                        text: root.confirmStop ? "Stop lesson and apply" : "Apply — new vehicle"
                        onClicked: root.apply()
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 6 }

                // --- simulation ---
                SectionTitle { text: "SIMULATION" }
                SettingRow {
                    label: "Start the simulation when the app opens"
                    hint: "Off: the app opens paused at 0 s and nothing happens on the bus until you press Run (or Space). "
                          + "Lessons and --scenario always start by themselves."
                    Switch {
                        checked: App.startOnLaunch
                        onToggled: App.startOnLaunch = checked
                    }
                }
                SettingRow {
                    label: "Speed at start"
                    hint: "Simulation speed factor the app opens with. --speed on the command line overrides it."
                    ComboBox {
                        id: speedBox
                        readonly property var speeds: [0.25, 0.5, 1, 2, 5, 10]
                        implicitWidth: 96
                        model: speeds.map(s => s + "×")
                        currentIndex: Math.max(0, speeds.indexOf(App.defaultSpeed))
                        onActivated: index => App.defaultSpeed = speeds[index]
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 6 }

                // --- display ---
                SectionTitle { text: "DISPLAY" }
                SettingRow {
                    label: "Reduce motion"
                    hint: "Stops the decorative animations (frames travelling on the bus, exhaust, pulsing fault markers, "
                          + "gauge sweep). Lamps, needles and doors still change state."
                    Switch {
                        checked: App.reducedMotion
                        onToggled: App.reducedMotion = checked
                    }
                }

                Item { implicitHeight: 12 }
            }
        }
    }
}
