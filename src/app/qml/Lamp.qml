import QtQuick
import QtQuick.Controls

// Warning lamp / tell-tale in the instrument cluster.
Rectangle {
    id: root
    property bool on: false
    property string label: ""
    property string tip: ""
    property color tone: Theme.warn

    implicitWidth: Math.max(26, text.implicitWidth + 8)
    implicitHeight: 24
    radius: 4
    color: on ? Qt.rgba(tone.r, tone.g, tone.b, 0.22) : "#15181d"
    border.color: on ? tone : "#262b34"

    Text {
        id: text
        anchors.centerIn: parent
        text: root.label
        color: root.on ? root.tone : "#3b414d"
        font { pixelSize: 9; bold: true }
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && root.tip !== ""
    ToolTip.delay: 400
    ToolTip.text: root.tip + (root.on ? "  —  ON" : "  —  off")
}
