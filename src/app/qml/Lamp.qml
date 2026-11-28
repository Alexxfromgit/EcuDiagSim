import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Warning lamp / tell-tale in the instrument cluster. Lights up with a soft glow, like a
// backlit symbol, and shrinks with the cluster on narrow windows.
Rectangle {
    id: root
    property bool on: false
    property string label: ""
    property string tip: ""
    property color tone: Theme.warn
    // How far the glow reaches past the lamp; a tight grid passes half its gap so glows don't overlap.
    property real haloSpread: 4

    implicitWidth: Math.max(24, metrics.advanceWidth + 10)
    TextMetrics { id: metrics; text: root.label; font.pixelSize: 9; font.bold: true }
    implicitHeight: 24
    Layout.fillWidth: true
    Layout.minimumWidth: 20
    Layout.maximumWidth: implicitWidth + 10
    radius: 4
    color: on ? Qt.rgba(tone.r, tone.g, tone.b, 0.24) : "#15181d"
    border.color: on ? tone : "#262b34"
    Behavior on color { ColorAnimation { duration: 90 } }

    // halo
    Rectangle {
        z: -1
        anchors.centerIn: parent
        width: parent.width + 2 * root.haloSpread; height: parent.height + 2 * root.haloSpread
        radius: parent.radius + root.haloSpread
        color: Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.16)
        border.color: Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.12)
        opacity: root.on ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Text {
        id: text
        anchors.centerIn: parent
        width: parent.width - 4
        horizontalAlignment: Text.AlignHCenter
        text: root.label
        color: root.on ? Qt.lighter(root.tone, 1.15) : "#3b414d"
        font { pixelSize: 9; bold: true }
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 7
        Behavior on color { ColorAnimation { duration: 90 } }
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && root.tip !== ""
    ToolTip.delay: 400
    ToolTip.text: root.tip + (root.on ? "  —  ON" : "  —  off")
}
