import QtQuick
import QtQuick.Shapes

// Round dial: the face is painted once; the needle and the lit arc move.
Item {
    id: root
    property real value: 0
    property real minimum: 0
    property real maximum: 240
    property real majorStep: 20
    property int minorPerMajor: 2
    property real labelDivisor: 1
    property real redFrom: Infinity
    property string unit: ""
    property string caption: ""
    property bool lit: true
    property int decimals: 0

    readonly property real startAngle: 135   // degrees, clockwise from 3 o'clock
    readonly property real sweep: 270
    readonly property real clamped: Math.max(minimum, Math.min(maximum, value))
    // The arc follows in steps of 0.25 % of the range: the path is rebuilt only when that changes,
    // not on every frame.
    readonly property real arcFraction: Math.round((clamped - minimum) / (maximum - minimum) * 400) / 400

    implicitWidth: 200
    implicitHeight: 200

    onLitChanged: face.requestPaint()

    Canvas {
        id: face
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            const w = width, h = height
            const cx = w / 2, cy = h / 2
            const r = Math.min(w, h) / 2 - 4
            ctx.reset()

            ctx.beginPath()
            ctx.arc(cx, cy, r, 0, 2 * Math.PI)
            ctx.fillStyle = "#0c0e12"
            ctx.fill()
            ctx.lineWidth = 2
            ctx.strokeStyle = "#343b48"
            ctx.stroke()

            const toRad = a => a * Math.PI / 180
            const angleOf = v => toRad(root.startAngle + (v - root.minimum) / (root.maximum - root.minimum) * root.sweep)
            const dim = root.lit ? 1.0 : 0.35

            if (root.redFrom < root.maximum) {
                ctx.beginPath()
                ctx.arc(cx, cy, r - 7, angleOf(root.redFrom), angleOf(root.maximum))
                ctx.lineWidth = 6
                ctx.strokeStyle = Qt.rgba(1, 0.3, 0.37, 0.9 * dim)
                ctx.stroke()
            }

            const minorStep = root.majorStep / root.minorPerMajor
            const count = Math.round((root.maximum - root.minimum) / minorStep)
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.font = "bold " + Math.round(r * 0.13) + "px sans-serif"
            for (let i = 0; i <= count; ++i) {
                const v = root.minimum + i * minorStep
                const a = angleOf(v)
                const major = i % root.minorPerMajor === 0
                const r1 = r - 4, r2 = r - (major ? 16 : 10)
                ctx.beginPath()
                ctx.moveTo(cx + r1 * Math.cos(a), cy + r1 * Math.sin(a))
                ctx.lineTo(cx + r2 * Math.cos(a), cy + r2 * Math.sin(a))
                ctx.lineWidth = major ? 2 : 1
                ctx.strokeStyle = Qt.rgba(0.89, 0.91, 0.93, (major ? 0.95 : 0.5) * dim)
                ctx.stroke()
                if (major) {
                    ctx.fillStyle = v >= root.redFrom ? Qt.rgba(1, 0.4, 0.45, dim) : Qt.rgba(0.89, 0.91, 0.93, dim)
                    ctx.fillText(String(Math.round(v / root.labelDivisor)), cx + (r - 30) * Math.cos(a), cy + (r - 30) * Math.sin(a))
                }
            }
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.38
        text: root.caption
        color: Theme.textDim
        font.pixelSize: 10
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.66
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.lit ? root.value.toFixed(root.decimals) : "–"
            color: Theme.text
            font { family: Theme.mono; pixelSize: 20; bold: true }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.unit
            color: Theme.textDim
            font.pixelSize: 10
        }
    }

    // lit arc from zero to the current value
    Shape {
        id: arc
        readonly property real r: Math.min(root.width, root.height) / 2 - 4
        anchors.fill: parent
        visible: root.lit && root.arcFraction > 0
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"
            strokeWidth: 3
            capStyle: ShapePath.RoundCap
            strokeColor: root.clamped >= root.redFrom ? Qt.rgba(1, 0.35, 0.3, 0.85) : Qt.rgba(0.29, 0.66, 1, 0.75)
            PathAngleArc {
                centerX: root.width / 2; centerY: root.height / 2
                radiusX: arc.r - 2; radiusY: arc.r - 2
                startAngle: root.startAngle
                sweepAngle: root.arcFraction * root.sweep
            }
        }
    }

    // needle
    Item {
        anchors.centerIn: parent
        width: 1; height: 1
        rotation: root.startAngle + (root.clamped - root.minimum) / (root.maximum - root.minimum) * root.sweep
        Rectangle {
            x: -8; y: -1.5
            width: Math.min(root.width, root.height) / 2 - 22 + 8
            height: 3
            radius: 1.5
            color: root.lit ? "#ff5a4d" : "#5a2a28"
            antialiasing: true
        }
        // soft glow along the lit needle
        Rectangle {
            z: -1
            visible: root.lit
            x: -6; y: -4
            width: Math.min(root.width, root.height) / 2 - 22 + 4
            height: 8
            radius: 4
            color: Qt.rgba(1, 0.35, 0.3, 0.18)
            antialiasing: true
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: 16; height: 16; radius: 8
        color: "#20252e"
        border.color: "#4a5262"
    }
}
