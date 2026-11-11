import QtQuick

// Small coloured label, used for states and status bits.
Rectangle {
    id: root
    property string text: ""
    property color tone: Theme.textDim
    property bool filled: false

    implicitWidth: label.implicitWidth + 12
    implicitHeight: 18
    radius: 9
    color: filled ? tone : Qt.rgba(tone.r, tone.g, tone.b, 0.14)
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.55)

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.filled ? "#0b0d10" : root.tone
        font { pixelSize: 10; bold: true }
    }
}
