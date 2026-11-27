import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

ApplicationWindow {
    id: window
    // Preferred size; main.cpp shrinks it to the available area of the window's own screen
    // (e.g. 1366x768 laptops, or 1920x1080 at 150 % scaling = 1280x720 logical pixels).
    width: 1600
    height: 940
    minimumWidth: 1024
    minimumHeight: 600
    visible: true
    title: "ECU Diagnostics Simulator"
    color: Theme.bg

    // 0 faults, 1 CAN trace, 2 scanner, 3 scenarios
    property int currentTab: 0
    property bool vehicle3DAvailable: false
    property bool vehicle3DDefault: false
    // Below this width the vehicle view is hidden by default to keep the tabs usable.
    readonly property bool narrow: width < 1200
    // The user's explicit choice (true / false) wins; undefined = follow the window width.
    property var carChoice: undefined
    readonly property bool carVisible: carChoice === undefined ? !narrow : carChoice
    // Column widths grow with the window; the tabs on the right take the rest.
    readonly property real carWidth: Math.max(330, Math.min(470, width * 0.24))
    readonly property real clusterWidth: Math.max(420, Math.min(580, width * 0.29))

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

    Shortcut { sequence: "Space"; enabled: !settings.visible; onActivated: App.running = !App.running }
    Shortcut { sequence: "Ctrl+,"; onActivated: settings.open() }

    SettingsPage { id: settings }
    // Tooltips must never cover the element they describe, or they swallow its clicks.
    ToolTipPlacement {}
    function openSettings() { settings.open() }
    function openVehicle3D() {
        carChoice = true
        vehicleWorkspace.open3D()
    }
    function openUds(index) {
        App.scanner.target = index
        scannerPanel.showUds()
        window.currentTab = 2
    }
    Shortcut { sequence: "F10"; enabled: !settings.visible; onActivated: App.step(10) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        SimBar { Layout.fillWidth: true; onSettingsRequested: settings.open() }

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

            VehicleWorkspace {
                id: vehicleWorkspace
                threeDAvailable: window.vehicle3DAvailable
                show3D: window.vehicle3DDefault
                visible: window.carVisible
                Layout.preferredWidth: window.carWidth
                Layout.maximumWidth: window.carWidth
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
                Layout.preferredWidth: window.clusterWidth
                Layout.maximumWidth: window.clusterWidth
                Layout.fillHeight: true
                spacing: 10
                Cluster {
                    id: cluster
                    // As tall as the gauges need for the column width, no empty bands.
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: cluster.idealHeight
                    Layout.minimumHeight: 220
                    Layout.maximumHeight: cluster.idealHeight
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
                        text: window.carVisible ? "⇤ Wide" : "⇥ Show car"
                        flat: true
                        onClicked: window.carChoice = !window.carVisible
                        ToolTip.visible: hovered
                        ToolTip.text: window.carVisible ? "Hide the vehicle view to make room for the trace"
                                                        : "Show the vehicle view"
                    }
                }
                StackLayout {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.currentTab
                    // The incoming panel fades in, so a tab switch reads as a change, not a flicker.
                    onCurrentIndexChanged: {
                        tabFade.stop()
                        const incoming = stack.children[currentIndex]
                        if (App.reducedMotion) {
                            incoming.opacity = 1
                            return
                        }
                        tabFade.target = incoming
                        tabFade.restart()
                    }
                    NumberAnimation { id: tabFade; property: "opacity"; from: 0.0; to: 1.0; duration: 160; easing.type: Easing.OutCubic }
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
                visible: window.width >= 1360
                // The status message on the left keeps at least half of the row.
                Layout.maximumWidth: window.width * 0.5
                text: window.width >= 1800
                      ? "Space  run / pause    F10  single step    Ctrl+,  settings    Click a part of the car to work with it"
                      : "Space  run / pause    F10  single step    Ctrl+,  settings"
                color: Theme.off
                font.pixelSize: 11
                elide: Text.ElideLeft
            }
        }
    }
}
