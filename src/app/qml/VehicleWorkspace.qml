import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EcuDiagSim

Item {
    id: root
    property bool threeDAvailable: false
    property bool show3D: false
    signal targetPicked(string id)
    signal ecuPicked(int index)

    function open3D() {
        if (!threeDAvailable)
            return
        show3D = true
        expanded.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Button {
                text: "3D"
                checkable: true
                checked: root.show3D
                enabled: root.threeDAvailable
                onClicked: root.show3D = true
                ToolTip.visible: hovered
                ToolTip.text: enabled ? "Explore the vehicle in 3D" : "This build includes the vehicle diagram only"
            }
            Button {
                text: "Diagram"
                checkable: true
                checked: !root.show3D
                onClicked: root.show3D = false
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Expand"
                enabled: root.threeDAvailable && root.show3D
                onClicked: expanded.open()
            }
        }
        Item {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                anchors.centerIn: parent
                visible: expanded.visible
                text: "Vehicle workspace expanded"
                color: Theme.textDim
                font.pixelSize: 12
            }
        }
    }

    // Reparent the same view when expanded, preserving its camera and selection.
    Loader {
        id: view
        parent: expanded.visible ? expanded.contentItem : viewport
        anchors.fill: parent
        source: root.threeDAvailable && root.show3D ? "Vehicle3D.qml" : "VehicleView.qml"
    }
    Connections {
        target: view.item
        function onTargetPicked(id: string) {
            root.targetPicked(id)
            expanded.close()
        }
        function onEcuPicked(index: int) {
            root.ecuPicked(index)
            expanded.close()
        }
    }
    Connections {
        target: root.show3D ? view.item : null
        ignoreUnknownSignals: true
        function onFallbackRequested() {
            expanded.close()
            root.show3D = false
        }
    }

    Dialog {
        id: expanded
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(1380, parent ? parent.width - 32 : 1100)
        height: Math.min(960, parent ? parent.height - 32 : 760)
        title: "Vehicle explorer"
        modal: true
        standardButtons: Dialog.Close
        contentItem: Item {}
    }
}
