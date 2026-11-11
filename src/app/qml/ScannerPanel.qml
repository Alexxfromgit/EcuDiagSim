import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

Panel {
    id: root

    readonly property var scanner: App.scanner
    readonly property bool uds: modeTabs.currentIndex === 1

    function logColor(kind) {
        switch (kind) {
        case "tx": return Theme.accent
        case "rx": return Theme.ok
        case "neg": return Theme.warn
        case "error": return Theme.error
        default: return Theme.textDim
        }
    }
    function logPrefix(kind) {
        switch (kind) {
        case "tx": return "→ "
        case "rx": return "← "
        case "neg": return "← "
        case "error": return "✕ "
        default: return "   "
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TabBar {
                id: modeTabs
                Layout.preferredWidth: 300
                onCurrentIndexChanged: root.scanner.pollMode = 0
                TabButton { text: "OBD-II  (generic)" }
                TabButton { text: "UDS  (per ECU)" }
            }
            Item { Layout.fillWidth: true }
            Chip {
                visible: App.ignition !== 1
                text: "IGNITION OFF — ONLY THE BCM ANSWERS"
                tone: Theme.warn
            }
        }

        // ----------------------------------------------------------- OBD-II
        ColumnLayout {
            visible: !root.uds
            Layout.fillWidth: true
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: "Legislated services, sent to the functional address 0x7DF. Every emission relevant ECU (here ECM and TCM) answers."
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Button { text: "Live data  · 01"; onClicked: root.scanner.obdLiveData() }
                Button { text: "Read codes  · 03 / 07"; highlighted: true; onClicked: root.scanner.obdReadCodes() }
                Button { text: "Freeze frame  · 02"; onClicked: root.scanner.obdFreezeFrame() }
                Button { text: "Vehicle info  · 09"; onClicked: root.scanner.obdVehicleInfo() }
                Button { text: "Clear codes  · 04"; onClicked: root.scanner.obdClearCodes() }
                CheckBox {
                    text: "Refresh live data"
                    checked: root.scanner.pollMode === 1
                    onToggled: root.scanner.pollMode = checked ? 1 : 0
                }
            }
        }

        // -------------------------------------------------------------- UDS
        ColumnLayout {
            visible: root.uds
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                spacing: 8
                Text { text: "Control unit"; color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    Layout.preferredWidth: 150
                    model: root.scanner.ecuNames
                    currentIndex: root.scanner.target
                    onActivated: index => root.scanner.target = index
                }
                Chip {
                    text: root.scanner.sessionText.toUpperCase()
                    tone: root.scanner.sessionText.startsWith("Ext") ? Theme.accent : Theme.textDim
                }
                Chip {
                    text: root.scanner.unlocked ? "UNLOCKED" : "LOCKED"
                    tone: root.scanner.unlocked ? Theme.ok : Theme.textDim
                }
                Item { Layout.fillWidth: true }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Button { text: "Read DTCs  · 19 02"; highlighted: true; onClicked: root.scanner.udsReadDtcs(0x0D) }
                Button { text: "All supported  · 19 0A"; onClicked: root.scanner.udsSupportedDtcs() }
                Button { text: "Clear DTCs  · 14"; onClicked: root.scanner.udsClearDtcs() }
                Button { text: "Identification  · 22"; onClicked: root.scanner.udsIdentification() }
                Button { text: "Live data  · 22"; onClicked: root.scanner.udsLiveData() }
                CheckBox {
                    text: "Refresh"
                    checked: root.scanner.pollMode === 2
                    onToggled: root.scanner.pollMode = checked ? 2 : 0
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Button { text: "Default session  · 10 01"; onClicked: root.scanner.udsSession(1) }
                Button { text: "Extended session  · 10 03"; onClicked: root.scanner.udsSession(3) }
                Button { text: "Unlock  · 27"; onClicked: root.scanner.udsUnlock() }
                CheckBox {
                    text: "Tester present  · 3E 80"
                    checked: root.scanner.keepAlive
                    onToggled: root.scanner.keepAlive = checked
                    ToolTip.visible: hovered
                    ToolTip.text: "Without it the ECU drops back to the default session after 5 s (S3 timeout)"
                }
                Button { text: "ECU reset  · 11 01"; onClicked: root.scanner.udsReset() }
            }
            RowLayout {
                visible: root.scanner.ioControls.length > 0
                spacing: 6
                Text { text: "Actuator test  · 2F"; color: Theme.textDim; font.pixelSize: 12 }
                Repeater {
                    model: root.scanner.ioControls
                    RowLayout {
                        id: io
                        required property var modelData
                        spacing: 2
                        Text { text: io.modelData.name; color: Theme.text; font.pixelSize: 12; Layout.leftMargin: 8 }
                        Button { text: "On"; implicitWidth: 44; onClicked: root.scanner.udsIoControl(io.modelData.did, 1) }
                        Button { text: "Off"; implicitWidth: 44; onClicked: root.scanner.udsIoControl(io.modelData.did, 0) }
                        Button { text: "Release"; onClicked: root.scanner.udsIoControl(io.modelData.did, -1) }
                    }
                }
                Text {
                    text: "needs extended session + unlock"
                    color: Theme.off
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                spacing: 6
                Text { text: "Raw request"; color: Theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: raw
                    Layout.fillWidth: true
                    placeholderText: "hex bytes, e.g.  22 F1 90   or   19 04 40 35 13 01"
                    font.family: Theme.mono
                    selectByMouse: true
                    onAccepted: root.scanner.sendRaw(text, functional.checked)
                }
                CheckBox { id: functional; text: "to 0x7DF" }
                Button { text: "Send"; onClicked: root.scanner.sendRaw(raw.text, functional.checked) }
            }
        }

        // ------------------------------------------------------- results
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 300
            spacing: 8

            // DTC table
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 380
                color: Theme.inset
                border.color: Theme.border
                radius: 4
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: root.scanner.dtcTitle === "" ? "TROUBLE CODES" : root.scanner.dtcTitle
                        color: Theme.textDim
                        font { pixelSize: 11; bold: true }
                        elide: Text.ElideRight
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: root.scanner.dtcs
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: dtc
                            required property var modelData
                            width: ListView.view.width - 10
                            height: content.implicitHeight + 12
                            radius: 4
                            color: dtcArea.containsMouse && !dtc.modelData.obd ? Theme.panelAlt : Theme.panel
                            border.color: dtc.modelData.active ? Theme.error : (dtc.modelData.confirmed ? Theme.warn : Theme.border)
                            ColumnLayout {
                                id: content
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                                spacing: 3
                                RowLayout {
                                    spacing: 6
                                    Text {
                                        text: dtc.modelData.code + (dtc.modelData.ftb !== "" ? "-" + dtc.modelData.ftb : "")
                                        color: Theme.text
                                        font { family: Theme.mono; pixelSize: 14; bold: true }
                                    }
                                    Chip { text: dtc.modelData.ecu; tone: Theme.accent }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        visible: !dtc.modelData.obd
                                        text: "status " + dtc.modelData.statusHex
                                        color: Theme.textDim
                                        font { family: Theme.mono; pixelSize: 11 }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: dtc.modelData.text + (dtc.modelData.ftbText !== "" ? "  —  " + dtc.modelData.ftbText : "")
                                    color: Theme.text
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Chip { visible: dtc.modelData.active; text: "ACTIVE NOW"; tone: Theme.error }
                                    Chip { visible: dtc.modelData.confirmed; text: "CONFIRMED"; tone: Theme.warn }
                                    Chip { visible: dtc.modelData.pending; text: "PENDING"; tone: Theme.accent }
                                    Chip { visible: dtc.modelData.lamp; text: "LAMP REQUESTED"; tone: Theme.warn }
                                    Chip {
                                        visible: !dtc.modelData.obd && !dtc.modelData.active && !dtc.modelData.confirmed && !dtc.modelData.pending
                                        text: (dtc.modelData.status & 0x10) ? "NOT TESTED SINCE CLEAR" : "TESTED, NO FAULT"
                                        tone: Theme.textDim
                                    }
                                }
                            }
                            MouseArea {
                                id: dtcArea
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !dtc.modelData.obd
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.scanner.udsSnapshot(dtc.modelData.fullCode)
                            }
                            ToolTip.visible: dtcArea.containsMouse && !dtc.modelData.obd
                            ToolTip.delay: 500
                            ToolTip.text: "Status bits: " + dtc.modelData.statusNames.join(", ") + "\n\nClick to read the snapshot (19 04) and extended data (19 06)"
                        }
                    }
                    Text {
                        visible: root.scanner.dtcs.length === 0
                        Layout.fillWidth: true
                        text: root.scanner.dtcTitle === "" ? "Press “Read codes”." : "No trouble codes."
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }

            // value table
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 300
                color: Theme.inset
                border.color: Theme.border
                radius: 4
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: root.scanner.valuesTitle === "" ? "DATA" : root.scanner.valuesTitle
                        color: Theme.textDim
                        font { pixelSize: 11; bold: true }
                        elide: Text.ElideRight
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.scanner.values
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: val
                            required property var modelData
                            required property int index
                            width: ListView.view.width - 10
                            height: Math.max(22, valueText.implicitHeight + 6)
                            color: index % 2 ? "transparent" : Theme.panel
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6
                                Text {
                                    text: val.modelData.ecu
                                    color: Theme.accent
                                    font { pixelSize: 10; bold: true }
                                    Layout.preferredWidth: 28
                                }
                                Text {
                                    text: val.modelData.name
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 120
                                }
                                Text {
                                    id: valueText
                                    text: val.modelData.value + (val.modelData.unit !== "" ? " " + val.modelData.unit : "")
                                    color: Theme.text
                                    font { family: Theme.mono; pixelSize: 12 }
                                    horizontalAlignment: Text.AlignRight
                                    wrapMode: Text.WrapAnywhere
                                    Layout.maximumWidth: 190
                                }
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------ log
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 190
            color: Theme.inset
            border.color: Theme.border
            radius: 4
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                RowLayout {
                    Text {
                        text: "COMMUNICATION LOG"
                        color: Theme.textDim
                        font { pixelSize: 11; bold: true }
                        Layout.fillWidth: true
                    }
                    ToolButton { text: "Clear"; onClicked: root.scanner.clearLog() }
                }
                ListView {
                    id: logView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.scanner.log
                    onCountChanged: Qt.callLater(positionViewAtEnd)
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Text {
                        required property var modelData
                        width: ListView.view.width - 10
                        text: (modelData.kind === "info" ? "            " : modelData.time.padStart(9) + "  ")
                              + root.logPrefix(modelData.kind) + modelData.text
                        color: root.logColor(modelData.kind)
                        font { family: Theme.mono; pixelSize: 11 }
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
