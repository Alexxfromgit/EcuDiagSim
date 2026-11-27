import QtQuick
import QtQuick.Controls
import QtWebEngine
import QtWebChannel
import EcuDiagSim

Rectangle {
    id: root
    color: Theme.inset
    border.color: Theme.border
    radius: Theme.radius
    clip: true
    signal targetPicked(string id)
    signal ecuPicked(int index)
    signal fallbackRequested()
    property bool ready: false
    property string failure: ""

    function refreshSnapshot() {
        const v = App.vehicle
        bridge.stateJson = JSON.stringify({
            targets: App.faultTargets, sensors: App.sensors, ecus: App.ecus,
            faults: App.activeFaults, running: App.running, reducedMotion: App.reducedMotion,
            fitted: App.fitted, speedFactor: App.speedFactor,
            vehicle: { speed: v.speed, lowBeam: v.lowBeam, highBeam: v.highBeam,
                       parkLamps: v.parkLamps, bulbLowLeftOpen: v.bulbLowLeftOpen,
                       braking: v.brake > 0.02, reversing: v.ignition && v.selector === 1 }
        })
    }

    // Publish only a display snapshot and validated navigation actions, never the App singleton.
    QtObject {
        id: bridge
        WebChannel.id: "vehicleBridge"
        property string stateJson: "{}"
        property bool displayActive: root.visible
        function viewerReady() {
            root.ready = true
            root.failure = ""
            root.refreshSnapshot()
        }
        function viewerFailed(message: string) {
            root.failure = message.substring(0, 300)
            root.ready = false
        }
        function openTarget(id: string) {
            for (const target of App.faultTargets) {
                if (target.id === id) {
                    root.targetPicked(id)
                    return
                }
            }
        }
        function openEcu(index: int) {
            for (const ecu of App.ecus) {
                if (ecu.diagIndex === index && index >= 0) {
                    root.ecuPicked(index)
                    return
                }
            }
        }
    }
    WebChannel { id: channel; registeredObjects: [bridge] }

    WebEngineView {
        id: browser
        anchors.fill: parent
        anchors.margins: 1
        url: "qrc:/vehicle-viewer/index.html"
        backgroundColor: root.color
        webChannel: channel
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: false
        settings.localStorageEnabled: false
        settings.pluginsEnabled: false
        settings.javascriptCanOpenWindows: false
        onNavigationRequested: request => {
            if (request.url.toString() !== "qrc:/vehicle-viewer/index.html")
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }
        onLoadingChanged: request => {
            if (request.status === WebEngineView.LoadFailedStatus)
                root.failure = "The vehicle viewer could not be loaded."
        }
        onRenderProcessTerminated: (terminationStatus, exitCode) => {
            root.ready = false
            root.failure = "The vehicle renderer stopped. Try reloading or use the diagram."
        }
    }
    Timer {
        interval: 100
        running: root.visible && root.ready
        repeat: true
        onTriggered: root.refreshSnapshot()
    }
    Timer {
        interval: 15000
        running: root.visible && !root.ready && root.failure === ""
        onTriggered: root.failure = "3D graphics did not become available. Try reloading or use the diagram."
    }
    Rectangle {
        anchors.fill: parent
        visible: !root.ready || root.failure !== ""
        color: Theme.inset
        Column {
            anchors.centerIn: parent
            width: Math.min(360, parent.width - 32)
            spacing: 12
            Text {
                width: parent.width
                text: root.failure || "Preparing the vehicle…"
                color: Theme.text
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.failure !== ""
                text: "Reload 3D view"
                onClicked: {
                    root.ready = false
                    root.failure = ""
                    browser.reload()
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Use diagram"
                onClicked: root.fallbackRequested()
            }
        }
    }
}
