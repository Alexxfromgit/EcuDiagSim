import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import EcuDiagSim

Panel {
    id: root

    readonly property var trace: App.trace
    // column widths: time, id, name, sender, dlc, data
    readonly property var cols: [58, 30, 116, 34, 14, 168]

    FileDialog {
        id: saveDialog
        title: "Save CAN trace"
        fileMode: FileDialog.SaveFile
        nameFilters: ["candump log (*.log)", "Vector ASC (*.asc)"]
        defaultSuffix: selectedNameFilter.index === 1 ? "asc" : "log"
        onAccepted: App.saveTrace(selectedFile)
    }
    FileDialog {
        id: openDialog
        title: "Replay a CAN log"
        fileMode: FileDialog.OpenFile
        nameFilters: ["CAN logs (*.log *.asc *.txt)", "All files (*)"]
        onAccepted: App.loadReplay(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: filter
                Layout.preferredWidth: 150
                placeholderText: "Filter: 0B0, ABS, diag"
                selectByMouse: true
                onTextChanged: root.trace.filter = text
                ToolTip.visible: hovered && text === ""
                ToolTip.delay: 600
                ToolTip.text: "Matches ID, message name or sender. Several terms separated by spaces are alternatives."
            }
            Button {
                text: "By ID"
                checkable: true
                checked: root.trace.fixedMode
                highlighted: checked
                onClicked: root.trace.fixedMode = !root.trace.fixedMode
                ToolTip.visible: hovered
                ToolTip.text: "One line per identifier with the measured cycle time, instead of a scrolling log"
            }
            Button {
                text: root.trace.paused ? "Resume" : "Freeze"
                checkable: true
                checked: root.trace.paused
                highlighted: checked
                onClicked: root.trace.paused = !root.trace.paused
            }
            Button { text: "Clear"; onClicked: root.trace.clear() }
            Item { Layout.fillWidth: true }
            Text {
                text: root.trace.count + (root.trace.fixedMode ? " IDs" : " frames")
                color: Theme.textDim
                font { family: Theme.mono; pixelSize: 11 }
            }
            Button { text: "Save…"; onClicked: saveDialog.open() }
            Button { text: "Replay…"; onClicked: openDialog.open() }
        }

        // header
        Rectangle {
            Layout.fillWidth: true
            height: 22
            color: Theme.panelAlt
            radius: 3
            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                spacing: 8
                Repeater {
                    model: [
                        { t: root.trace.fixedMode ? "Cycle ms" : "Time s", w: root.cols[0] },
                        { t: "ID", w: root.cols[1] },
                        { t: "Message", w: root.cols[2] },
                        { t: "From", w: root.cols[3] },
                        { t: "N", w: root.cols[4] },
                        { t: "Data", w: root.cols[5] },
                        { t: "Decoded signals", w: 300 }
                    ]
                    Text {
                        required property var modelData
                        width: modelData.w
                        height: 22
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.t
                        color: Theme.textDim
                        font { pixelSize: 10; bold: true }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.inset
            border.color: Theme.border
            radius: 4

            ListView {
                id: view
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.trace
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                // Follow the newest frame unless the user scrolled up.
                property bool follow: true
                onMovementStarted: follow = false
                onMovementEnded: follow = atYEnd
                // The row count stays constant once the buffer is full, so follow insertions instead.
                Connections {
                    target: root.trace
                    function onRowsInserted() {
                        if (view.follow && !root.trace.fixedMode)
                            Qt.callLater(view.positionViewAtEnd)
                    }
                }

                delegate: Rectangle {
                    id: line
                    required property int index
                    required property string time
                    required property string canId
                    required property int dlc
                    required property string bytes
                    required property string name
                    required property string decoded
                    required property string sender
                    required property int kind
                    required property string cycle

                    readonly property color ink: kind === 1 ? Theme.accent : (kind === 2 ? Theme.ok : Theme.text)
                    width: ListView.view.width
                    height: 19
                    color: kind !== 0 ? Qt.rgba(ink.r, ink.g, ink.b, 0.08) : (index % 2 ? "#12151a" : "transparent")

                    HoverHandler { id: lineHover }
                    ToolTip.visible: lineHover.hovered && line.decoded !== ""
                    ToolTip.delay: 500
                    ToolTip.text: line.name + "  (0x" + line.canId + ")
" + line.decoded.split("  ").join("
")
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        spacing: 8
                        Text { width: root.cols[0]; height: 19; verticalAlignment: Text.AlignVCenter; color: Theme.textDim; font { family: Theme.mono; pixelSize: 11 }
                               text: root.trace.fixedMode ? line.cycle : line.time }
                        Text { width: root.cols[1]; height: 19; verticalAlignment: Text.AlignVCenter; color: line.ink; font { family: Theme.mono; pixelSize: 11; bold: true }
                               text: line.canId }
                        Text { width: root.cols[2]; height: 19; verticalAlignment: Text.AlignVCenter; color: line.ink; font.pixelSize: 11; elide: Text.ElideRight
                               text: line.name }
                        Text { width: root.cols[3]; height: 19; verticalAlignment: Text.AlignVCenter; color: Theme.textDim; font.pixelSize: 11
                               text: line.sender }
                        Text { width: root.cols[4]; height: 19; verticalAlignment: Text.AlignVCenter; color: Theme.textDim; font { family: Theme.mono; pixelSize: 11 }
                               text: line.dlc }
                        Text { width: root.cols[5]; height: 19; verticalAlignment: Text.AlignVCenter; color: Theme.text; font { family: Theme.mono; pixelSize: 11 }
                               text: line.bytes }
                        Text { height: 19; verticalAlignment: Text.AlignVCenter; color: line.kind === 0 ? Theme.textDim : line.ink; font.pixelSize: 11
                               text: line.decoded }
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
            }

            Text {
                anchors.centerIn: parent
                visible: view.count === 0
                text: App.ignition === 1 ? "No frames match." : "Bus is quiet except for the BCM. Switch the ignition on."
                color: Theme.textDim
            }
            Button {
                anchors { right: parent.right; bottom: parent.bottom; margins: 22 }
                visible: !view.follow && !root.trace.fixedMode && view.count > 0
                text: "↓ Follow"
                onClicked: { view.follow = true; view.positionViewAtEnd() }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "Blue = diagnostic request (0x7DF, 0x7E0–0x7E7), green = response (0x7E8–0x7EF). Hover a line for all signals. "
                  + "Lower identifiers win arbitration: 0x0B0 always precedes 0x100 within a cycle."
            color: Theme.off
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
