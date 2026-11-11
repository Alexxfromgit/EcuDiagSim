import QtQuick
import QtQuick.Layouts

// A titled card. Children go below the title.
Rectangle {
    id: root
    property string title: ""
    property string subtitle: ""
    default property alias content: body.data
    property alias headerItems: headerRow.data

    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radius

    RowLayout {
        id: headerRow
        visible: root.title !== ""
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        height: visible ? 20 : 0
        spacing: 8
        Text {
            text: root.title.toUpperCase()
            color: Theme.textDim
            font { pixelSize: 11; bold: true; letterSpacing: 1.2 }
        }
        Text {
            text: root.subtitle
            color: Theme.textDim
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    Item {
        id: body
        anchors {
            left: parent.left; right: parent.right; bottom: parent.bottom
            top: headerRow.visible ? headerRow.bottom : parent.top
            margins: 10
            topMargin: headerRow.visible ? 6 : 10
        }
    }
}
