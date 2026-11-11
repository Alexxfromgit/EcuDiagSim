import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

ApplicationWindow {
    id: window
    width: 1600
    height: 940
    minimumWidth: 1280
    minimumHeight: 680
    visible: true
    title: "ECU Diagnostics Simulator"
    color: Theme.bg

    // 0 faults, 1 CAN trace, 2 scanner, 3 scenarios
    property int currentTab: 0
    // Hides the vehicle view to give the trace / scan tool more room.
    property bool wide: false

    palette {
        window: Theme.bg
        windowText: Theme.text
        base: Theme.inset
        alternateBase: Theme.panelAlt
        text: Theme.text
        button: "#2a303b"
        buttonText: Theme.text
        brightText: "#ffffff"
        highlight: Theme.accent
        highlightedText: "#0b0d10"
        placeholderText: Theme.textDim
        toolTipBase: "#2a303b"
        toolTipText: Theme.text
        mid: "#3a404c"
        dark: "#0c0e12"
        light: "#3a4250"
        midlight: "#323946"
        shadow: "#000000"
        link: Theme.accent
    }

    Shortcut { sequence: "Space"; onActivated: App.running = !App.running }
    Shortcut { sequence: "F10"; onActivated: App.step(10) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        SimBar { Layout.fillWidth: true }

        // Update ready / result of "Check for updates".
        Rectangle {
            readonly property bool ready: App.updates.updateReady
            visible: ready || App.updates.statusText !== ""
            Layout.fillWidth: true
            implicitHeight: 40
            radius: Theme.radius
            color: ready ? Qt.rgba(0.29, 0.66, 1, 0.14) : Theme.panel
            border.color: ready ? Theme.accent : Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 6
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: parent.parent.ready ? App.updates.readyText : App.updates.statusText
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                ProgressBar {
                    visible: App.updates.busy && App.updates.progress > 0
                    Layout.preferredWidth: 120
                    from: 0; to: 100
                    value: App.updates.progress
                }
                Button {
                    visible: parent.parent.ready
                    text: "Restart now"
                    highlighted: true
                    onClicked: App.updates.restartToUpdate()
                }
                Button {
                    visible: parent.parent.ready
                    text: "What's new"
                    onClicked: Qt.openUrlExternally(App.updates.releaseNotesUrl)
                }
                ToolButton {
                    text: "✕"
                    visible: !App.updates.busy
                    onClicked: App.updates.dismiss()
                    ToolTip.visible: hovered
                    ToolTip.text: parent.parent.ready ? "Later" : "Close"
                }
            }
        }

        Rectangle {
            visible: App.errorText !== ""
            Layout.fillWidth: true
            implicitHeight: 28
            color: Theme.error
            radius: 4
            Text { anchors.centerIn: parent; text: App.errorText; color: "white" }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            VehicleView {
                visible: !window.wide
                Layout.preferredWidth: 372
                Layout.maximumWidth: 372
                Layout.fillHeight: true
                onTargetPicked: id => {
                    faults.selectTarget(id)
                    window.currentTab = 0
                }
                onEcuPicked: index => {
                    App.scanner.target = index
                    scannerPanel.showUds()
                    window.currentTab = 2
                }
            }

            ColumnLayout {
                // Narrower on small screens so the tabs on the right keep enough room.
                Layout.preferredWidth: window.width < 1500 ? 420 : 470
                Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true
                spacing: 10
                Cluster {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 330
                    Layout.minimumHeight: 230
                    Layout.maximumHeight: 330
                }
                DriverControls { Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 250 }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    currentIndex: window.currentTab
                    onCurrentIndexChanged: window.currentTab = currentIndex
                    TabButton { text: "Faults" + (App.activeFaults.length > 0 ? "  (" + App.activeFaults.length + ")" : "") }
                    TabButton { text: "CAN trace" }
                    TabButton { text: "Scan tool" }
                    TabButton { text: "Lessons" }
                }
                    Button {
                        text: window.wide ? "⇥ Show car" : "⇤ Wide"
                        flat: true
                        onClicked: window.wide = !window.wide
                        ToolTip.visible: hovered
                        ToolTip.text: "Hide the vehicle view to make room for the trace"
                    }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.currentTab
                    FaultsPanel { id: faults }
                    TracePanel {}
                    ScannerPanel { id: scannerPanel }
                    ScenariosPanel {}
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                text: App.message
                color: Theme.textDim
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: "Space: run / pause    F10: single step    Click a sensor or control unit in the car to work with it"
                color: Theme.off
                font.pixelSize: 11
            }
        }
    }
}
