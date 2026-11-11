import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

Panel {
    id: root

    property int selectedIndex: 0
    readonly property var targets: App.faultTargets
    readonly property var target: targets[selectedIndex] ?? null
    readonly property var liveSensor: {
        if (!target)
            return null
        for (const s of App.sensors)
            if (s.id === target.id)
                return s
        return null
    }
    readonly property string selectedType: typeBox.currentValue ?? ""
    readonly property bool needsP1: ["stuck", "offset", "gain", "noise", "intermittent"].indexOf(selectedType) >= 0
    readonly property bool needsP2: selectedType === "intermittent"

    function selectTarget(id) {
        for (let i = 0; i < targets.length; ++i) {
            if (targets[i].id === id) {
                selectedIndex = i
                list.positionViewAtIndex(i, ListView.Contain)
                return
            }
        }
    }
    function isActive(id) {
        for (const f of App.activeFaults)
            if (f.targetId === id)
                return true
        return false
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // --- what can break ---
        ColumnLayout {
            Layout.preferredWidth: Math.min(280, root.width * 0.45)
            Layout.maximumWidth: 280
            Layout.fillHeight: true
            spacing: 6
            Text { text: "1 · PICK A COMPONENT"; color: Theme.textDim; font { pixelSize: 11; bold: true; letterSpacing: 1 } }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.inset
                border.color: Theme.border
                radius: 4
                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: root.targets
                    currentIndex: root.selectedIndex
                    section.property: "kind"
                    section.delegate: Rectangle {
                        required property string section
                        width: ListView.view.width
                        height: 22
                        color: Theme.panelAlt
                        Text {
                            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 8 }
                            text: parent.section.toUpperCase()
                            color: Theme.textDim
                            font { pixelSize: 10; bold: true; letterSpacing: 1 }
                        }
                    }
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property var modelData
                        readonly property bool active: App.activeFaults.length >= 0 && root.isActive(modelData.id)
                        width: ListView.view.width
                        height: 26
                        color: index === root.selectedIndex ? Qt.rgba(0.29, 0.66, 1, 0.22) : (rowArea.containsMouse ? Theme.panelAlt : "transparent")
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 6
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: row.active ? Theme.error : Theme.off
                            }
                            Text {
                                text: row.modelData.name
                                color: row.active ? Theme.error : Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: row.modelData.ecu
                                color: Theme.textDim
                                font { pixelSize: 10; bold: true }
                            }
                        }
                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.selectedIndex = row.index
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }
        }

        // --- how it breaks ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Text { text: "2 · CHOOSE THE FAULT"; color: Theme.textDim; font { pixelSize: 11; bold: true; letterSpacing: 1 } }
            Text {
                text: root.target ? root.target.name : ""
                color: Theme.text
                font { pixelSize: 16; bold: true }
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                visible: root.liveSensor !== null
                text: root.liveSensor
                      ? "real " + root.liveSensor.trueValue.toFixed(1) + " " + root.liveSensor.unit
                        + "    measured " + root.liveSensor.value.toFixed(1) + " " + root.liveSensor.unit
                        + (root.liveSensor.digital ? "" : "    pin " + root.liveSensor.voltage.toFixed(2) + " V")
                      : ""
                color: Theme.textDim
                font { family: Theme.mono; pixelSize: 12 }
            }

            RowLayout {
                spacing: 8
                ComboBox {
                    id: typeBox
                    Layout.preferredWidth: 220
                    Layout.fillWidth: true
                    Layout.maximumWidth: 220
                    model: root.target ? root.target.types : []
                    textRole: "label"
                    valueRole: "id"
                }
                TextField {
                    id: p1
                    visible: root.needsP1
                    Layout.preferredWidth: 90
                    placeholderText: root.selectedType === "intermittent" ? "period ms" : "value"
                    validator: DoubleValidator {}
                    selectByMouse: true
                }
                TextField {
                    id: p2
                    visible: root.needsP2
                    Layout.preferredWidth: 80
                    placeholderText: "open %"
                    validator: DoubleValidator {}
                    selectByMouse: true
                }
                Item { Layout.fillWidth: true }
            }
            Text {
                text: root.target ? App.faultHint(root.target.id, root.selectedType) : ""
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: 8
                Button {
                    text: "Inject fault"
                    highlighted: true
                    enabled: root.target !== null && root.selectedType !== ""
                    onClicked: App.injectFault(root.target.id, root.selectedType, root.needsP1 ? p1.text : "", root.needsP2 ? p2.text : "")
                }
                Button {
                    text: "Repair"
                    enabled: root.target !== null && App.activeFaults.length >= 0 && root.isActive(root.target.id)
                    onClicked: App.clearFault(root.target.id)
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: 6 }

            RowLayout {
                Text {
                    text: "ACTIVE FAULTS  ·  " + App.activeFaults.length
                    color: Theme.textDim
                    font { pixelSize: 11; bold: true; letterSpacing: 1 }
                    Layout.fillWidth: true
                }
                Button {
                    text: "Repair all"
                    enabled: App.activeFaults.length > 0
                    onClicked: App.clearAllFaults()
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 180)
                clip: true
                spacing: 4
                model: App.activeFaults
                delegate: Rectangle {
                    id: fault
                    required property var modelData
                    width: ListView.view.width
                    height: 32
                    radius: 4
                    color: Qt.rgba(1, 0.3, 0.37, 0.10)
                    border.color: Qt.rgba(1, 0.3, 0.37, 0.5)
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 4
                        Text {
                            text: fault.modelData.target
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Chip {
                            text: fault.modelData.type + (fault.modelData.detail !== "" ? "  " + fault.modelData.detail : "")
                            tone: Theme.error
                        }
                        ToolButton {
                            text: "✕"
                            onClicked: App.clearFault(fault.modelData.targetId)
                            ToolTip.visible: hovered
                            ToolTip.text: "Repair"
                        }
                    }
                }
            }
            Text {
                visible: App.activeFaults.length === 0
                text: "The vehicle is healthy. Start the engine, pick a component and break it — then watch the CAN trace, the cluster lamps and the fault memories."
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Item { Layout.fillHeight: true }

            // --- state of the control units ---
            Text { text: "CONTROL UNITS"; color: Theme.textDim; font { pixelSize: 11; bold: true; letterSpacing: 1 } }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: App.ecus
                    Rectangle {
                        id: ecuCard
                        required property var modelData
                        readonly property color tone: Theme.ecuColor(modelData.state)
                        width: 150; height: 44
                        radius: 4
                        color: Theme.inset
                        border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.7)
                        Column {
                            anchors { fill: parent; margins: 6 }
                            spacing: 2
                            Row {
                                spacing: 6
                                Rectangle { width: 8; height: 8; radius: 4; color: ecuCard.tone; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: ecuCard.modelData.name; color: Theme.text; font { pixelSize: 12; bold: true } }
                                Text {
                                    visible: ecuCard.modelData.dtcCount > 0
                                    text: ecuCard.modelData.dtcCount + " DTC"
                                    color: Theme.warn
                                    font { pixelSize: 11; bold: true }
                                }
                            }
                            Text {
                                text: Theme.ecuStateText(ecuCard.modelData.state)
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }
        }
    }
}
