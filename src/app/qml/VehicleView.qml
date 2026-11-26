pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import EcuDiagSim

// Top view of the car: control units on the CAN bus, sensors and components as hotspots.
// Head, park, tail and turn lamps show what the BCM really drives, so they go dark without the BCM;
// brake and reversing lamps are switched directly by the pedal and the selector and stay lit.
Panel {
    id: root
    title: "Vehicle"
    subtitle: "click a part to inject a fault, a control unit to diagnose it"

    signal targetPicked(string id)
    signal ecuPicked(int index)

    readonly property var v: App.vehicle
    readonly property var sensorMap: {
        const m = {}
        for (const s of App.sensors)
            m[s.id] = s
        return m
    }
    readonly property var ecuMap: {
        const m = {}
        for (const e of App.ecus)
            m[e.name] = e
        return m
    }
    // Where each fault target sits on the drawing (car coordinates). Targets without a place here
    // are still in the Faults tab; control units are drawn as boxes below.
    readonly property var spotPlaces: ({
        "wss_fl": { x: 81, y: 97, left: true }, "wss_fr": { x: 285, y: 97 },
        "wss_rl": { x: 81, y: 369, left: true }, "wss_rr": { x: 285, y: 369 },
        "ckp": { x: 150, y: 38, left: true }, "ect": { x: 183, y: 34 }, "tps": { x: 224, y: 38 },
        "tft": { x: 266, y: 128, left: true },
        "alternator": { x: 122, y: 100 }, "cooling": { x: 244, y: 100, left: true },
        "bulb_low_left": { x: 112, y: 2, left: true }, "abs_e2e": { x: 120, y: 162 },
        "throttle_body": { x: 194, y: 112 },
        "eop": { x: 104, y: 64 }, "oil_pump": { x: 104, y: 82 },
        "fls": { x: 196, y: 392 }, "park_brake_switch": { x: 236, y: 314 },
        // tyre pressure monitoring: the sensors sit on the valves
        "tpms_fl": { x: 81, y: 124, left: true }, "tpms_fr": { x: 285, y: 124 },
        "tpms_rl": { x: 81, y: 396, left: true }, "tpms_rr": { x: 285, y: 396 },
        // engine OBD pack
        "maf": { x: 244, y: 52 }, "o2": { x: 150, y: 180 },
        "ignition_coils": { x: 162, y: 12 }, "intake_leak": { x: 214, y: 12 }
    })
    readonly property var spots: App.faultTargets.filter(t => root.spotPlaces[t.id] !== undefined)
    readonly property var faultedIds: App.activeFaults.map(f => f.targetId)
    readonly property var doors: App.doors // changes with the inputs, not on every frame

    // Lamp states: BCM outputs, plus brake and reversing lamps wired to their switches.
    readonly property bool lowLeft: v.lowBeam && !v.bulbLowLeftOpen
    readonly property bool lowRight: v.lowBeam
    readonly property bool highBeam: v.highBeam
    readonly property bool parkLamps: v.parkLamps
    readonly property bool braking: v.brake > 0.02
    readonly property bool reversing: v.ignition && v.selector === 1
    readonly property bool animate: App.running && root.visible
    // Purely decorative loops (bus frames, exhaust, pulsing markers) also honour "Reduce motion".
    readonly property bool decorate: animate && !App.reducedMotion
    // Decorative motion follows the simulation speed, within limits that stay pleasant to watch.
    readonly property real timeScale: Math.max(0.5, Math.min(2.5, App.speedFactor))
    // Fresh vehicle that has never run.
    readonly property bool waiting: !App.running && App.simTime === 0 && !App.replaying

    // Distance rolled, drives the tyre tread animation.
    property real roll: 0
    FrameAnimation {
        running: root.animate && root.v.speed > 0.05
        onTriggered: {
            const r = root.roll + root.v.velocity / 3.6 * frameTime * App.speedFactor * 9
            root.roll = ((r % 8) + 8) % 8
        }
    }

    // --- colours -------------------------------------------------------------
    readonly property color warmWhite: "#fff1c9"
    readonly property color coolWhite: "#e4efff"
    readonly property color amber: "#ffad1f"
    readonly property color tailRed: "#ff2a3c"

    // --- building blocks -----------------------------------------------------
    // Soft round light source; centred on (cx, cy).
    component Glow: Shape {
        id: glow
        property real cx: 0
        property real cy: 0
        property real r: 16
        property color tint: "white"
        property real strength: 0.75
        property bool on: false
        x: cx - r; y: cy - r
        width: 2 * r; height: 2 * r
        opacity: on ? 1 : 0
        visible: opacity > 0.01
        preferredRendererType: Shape.CurveRenderer
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutQuad } }
        Behavior on r { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on strength { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.r; centerY: glow.r; centerRadius: glow.r
                focalX: glow.r; focalY: glow.r
                GradientStop { position: 0.0; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, glow.strength) }
                GradientStop { position: 0.45; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, glow.strength * 0.35) }
                GradientStop { position: 1.0; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, 0) }
            }
            PathAngleArc { centerX: glow.r; centerY: glow.r; radiusX: glow.r; radiusY: glow.r; startAngle: 0; sweepAngle: 360 }
        }
    }

    // Light cone in front of a headlamp: from (x0, y0) forward to yEnd, spreading by `spread`.
    component Beam: Shape {
        id: beam
        property real x0: 0
        property real y0: 0
        property real yEnd: -40
        property real halfWidth: 10
        property real spread: 18
        property real skew: 0
        property color tint: "white"
        property real strength: 0.4
        property bool on: false
        anchors.fill: parent
        opacity: on ? 1 : 0
        visible: opacity > 0.01
        preferredRendererType: Shape.CurveRenderer
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: beam.x0; y1: beam.y0; x2: beam.x0 + beam.skew; y2: beam.yEnd
                GradientStop { position: 0.0; color: Qt.rgba(beam.tint.r, beam.tint.g, beam.tint.b, beam.strength) }
                GradientStop { position: 1.0; color: Qt.rgba(beam.tint.r, beam.tint.g, beam.tint.b, 0) }
            }
            startX: beam.x0 - beam.halfWidth; startY: beam.y0
            PathLine { x: beam.x0 - beam.halfWidth - beam.spread + beam.skew; y: beam.yEnd + 8 }
            PathQuad {
                x: beam.x0 + beam.halfWidth + beam.spread + beam.skew; y: beam.yEnd + 8
                controlX: beam.x0 + beam.skew; controlY: beam.yEnd - 10
            }
            PathLine { x: beam.x0 + beam.halfWidth; y: beam.y0 }
            PathLine { x: beam.x0 - beam.halfWidth; y: beam.y0 }
        }
    }

    // A lamp lens on the body: dark when off, bright with a halo when on.
    component LampLens: Rectangle {
        id: lens
        property color tint: "white"
        property color offColor: "#2a303a"
        property bool on: false
        property real level: 1.0 // 0..1 brightness when on
        property real haloRadius: 14
        radius: Math.min(width, height) / 2
        color: on ? Qt.tint(offColor, Qt.rgba(tint.r, tint.g, tint.b, 0.35 + 0.65 * level)) : offColor
        border.color: on ? Qt.lighter(tint, 1.2) : "#454d5c"
        border.width: 1
        antialiasing: true
        Behavior on color { ColorAnimation { duration: 120 } }
        Glow {
            parent: lens.parent
            cx: lens.x + lens.width / 2
            cy: lens.y + lens.height / 2
            r: lens.haloRadius
            tint: lens.tint
            strength: 0.55 * lens.level
            on: lens.on
        }
    }

    component EcuBox: Rectangle {
        id: box
        property string name: ""
        readonly property var info: root.ecuMap[name] ?? ({ state: "sleep", dtcCount: 0, description: "", diagIndex: -1 })
        readonly property int diagIndex: info.diagIndex ?? -1
        readonly property color tone: Theme.ecuColor(info.state)
        readonly property bool alert: info.state === "fault" || info.state === "busoff" || info.state === "nopower"

        radius: 6
        gradient: Gradient {
            GradientStop { position: 0; color: box.info.state === "nopower" ? "#1a1c21" : Qt.rgba(box.tone.r, box.tone.g, box.tone.b, 0.22) }
            GradientStop { position: 1; color: box.info.state === "nopower" ? "#121418" : Qt.rgba(box.tone.r, box.tone.g, box.tone.b, 0.08) }
        }
        border.color: area.containsMouse ? Qt.lighter(tone, 1.3) : tone
        border.width: area.containsMouse ? 2 : 1

        // Halo while a fault is active: expands and fades like a ping (steady with reduced motion).
        Rectangle {
            id: halo
            z: -1
            anchors.centerIn: parent
            width: parent.width + 8; height: parent.height + 8
            radius: parent.radius + 4
            color: "transparent"
            border.color: box.tone
            border.width: 2
            visible: box.alert
            opacity: App.reducedMotion ? 0.5 : 0
            ParallelAnimation {
                running: box.alert && root.visible && !App.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { target: halo; property: "opacity"; from: 0.65; to: 0.0; duration: 1300; easing.type: Easing.OutCubic }
                NumberAnimation { target: halo; property: "scale"; from: 1.0; to: 1.22; duration: 1300; easing.type: Easing.OutCubic }
            }
        }

        Text {
            anchors.centerIn: parent
            text: box.name
            color: box.info.state === "sleep" || box.info.state === "nopower" ? Theme.textDim : Theme.text
            font { pixelSize: 12; bold: true; letterSpacing: 0.5 }
        }
        Rectangle {
            visible: box.info.dtcCount > 0
            anchors { right: parent.right; top: parent.top; margins: -7 }
            width: 18; height: 18; radius: 9
            color: Theme.warn
            border.color: "#0b0d10"
            Text {
                anchors.centerIn: parent
                text: box.info.dtcCount
                color: "#0b0d10"
                font { pixelSize: 10; bold: true }
            }
            onVisibleChanged: if (visible && !App.reducedMotion) pop.restart()
            NumberAnimation on scale { id: pop; running: false; from: 1.6; to: 1; duration: 260; easing.type: Easing.OutBack }
        }
        Text {
            visible: box.info.state === "nopower" || box.info.state === "busoff"
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.bottom; topMargin: 1 }
            text: box.info.state === "nopower" ? "NO POWER" : "BUS-OFF"
            color: box.info.state === "nopower" ? Theme.error : Theme.violet
            font { pixelSize: 8; bold: true }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton || box.diagIndex < 0)
                    root.targetPicked(box.name.toLowerCase())
                else
                    root.ecuPicked(box.diagIndex)
            }
        }
        ToolTip.visible: area.containsMouse
        ToolTip.delay: 300
        ToolTip.text: box.info.description + "\n" + Theme.ecuStateText(box.info.state)
            + (box.info.dtcCount > 0 ? "\n" + box.info.dtcCount + " DTC(s) stored" : "")
            + (box.diagIndex >= 0 ? "\n\nClick: open in scan tool\nRight click: power / bus faults" : "\n\nClick: power / bus faults")
    }

    component Hotspot: Item {
        id: spot
        property string targetId: ""
        property string label: ""
        property bool labelLeft: false
        property string name: ""
        readonly property var sensor: root.sensorMap[targetId]
        readonly property bool faulted: root.faultedIds.indexOf(targetId) >= 0
        z: 5

        width: 14; height: 14
        // Ripple while the fault is active.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width; height: parent.height; radius: width / 2
            color: "transparent"
            border.color: Theme.error
            border.width: 2
            visible: spot.faulted
            opacity: App.reducedMotion ? 0.6 : 0
            ParallelAnimation {
                running: spot.faulted && root.visible && !App.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { target: ripple; property: "scale"; from: 1; to: 2.6; duration: 1200; easing.type: Easing.OutCubic }
                NumberAnimation { target: ripple; property: "opacity"; from: 0.9; to: 0; duration: 1200; easing.type: Easing.OutCubic }
            }
            id: ripple
        }
        Rectangle {
            anchors.fill: parent
            radius: 7
            color: spot.faulted ? Theme.error : (spotArea.containsMouse ? "#25463b" : "#16241f")
            border.color: spot.faulted ? "#ffb3ba" : Theme.ok
            border.width: spotArea.containsMouse ? 2 : 1.5
            Rectangle {
                anchors.centerIn: parent
                width: 4; height: 4; radius: 2
                color: spot.faulted ? "#ffffff" : Theme.ok
            }
        }
        // Label on a dark pill so it stays readable over the body, the lamps and the background.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: spot.labelLeft ? -width - 3 : parent.width + 3
            width: labelText.implicitWidth + 8
            height: 13
            radius: 3
            color: spot.faulted ? Qt.rgba(1, 0.3, 0.37, 0.22) : "#cc0d1015"
            border.color: spot.faulted ? Theme.error : "#2c3340"
            Text {
                id: labelText
                anchors.centerIn: parent
                text: spot.label
                color: spot.faulted ? "#ffc2c8" : Theme.textDim
                font { pixelSize: 8; bold: true; letterSpacing: 0.4 }
            }
        }
        MouseArea {
            id: spotArea
            anchors.fill: parent
            anchors.margins: -5
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.targetPicked(spot.targetId)
        }
        ToolTip.visible: spotArea.containsMouse
        ToolTip.delay: 200
        ToolTip.text: {
            const s = spot.sensor
            if (!s)
                return (spot.name || spot.label) + (spot.faulted ? "\nFAULT ACTIVE" : "") + "\n\nClick to inject a fault"
            let t = s.name + "  →  " + s.ecu
                + "\nreal value:     " + s.trueValue.toFixed(1) + " " + s.unit
                + "\nECU measures:   " + s.value.toFixed(1) + " " + s.unit
            if (!s.digital)
                t += "\npin voltage:    " + s.voltage.toFixed(2) + " V"
            t += "\ncircuit:        " + s.circuit
            if (s.faulted)
                t += "\nFAULT:          " + s.fault
            return t + "\n\nClick to inject a fault"
        }
    }

    component Wire: Rectangle {
        color: root.v.busFailure ? Theme.error : "#c9a227"
        opacity: root.v.busFailure ? 0.55 + 0.45 * busBlink.value : 0.8
        radius: 1
    }

    component Wheel: Rectangle {
        id: tyre
        property real bar: 2.4 // cold pressure
        readonly property bool soft: bar < 2.1
        readonly property bool flat: bar < 1.8
        width: 24; height: 64; radius: 7
        color: "#0a0c0f"
        // A soft tyre bulges: the outline widens and turns amber, red below the warning threshold.
        border.color: flat ? Theme.error : (soft ? Theme.warn : "#3b4352")
        border.width: soft ? 2 : 1
        Behavior on border.color { ColorAnimation { duration: 300 } }
        clip: true
        // tread blocks rolling with the road speed
        Repeater {
            model: 10
            Rectangle {
                required property int index
                x: 3; width: tyre.width - 6; height: 2; radius: 1
                y: index * 8 + root.roll - 8
                color: "#262c36"
            }
        }
        // rim seen through the arch
        Rectangle {
            anchors { verticalCenter: parent.verticalCenter }
            x: (tyre.width - width) / 2
            width: 8; height: 40; radius: 3
            color: "#1b2027"
            border.color: "#4a5262"
        }
    }

    // Shared blink phase for the shorted CAN wiring.
    QtObject {
        id: busBlink
        property real value: 0
        SequentialAnimation on value {
            running: root.v.busFailure && root.visible
            loops: Animation.Infinite
            NumberAnimation { to: 1; duration: 350 }
            NumberAnimation { to: 0; duration: 350 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 240
            Layout.alignment: Qt.AlignHCenter

            Item {
                id: car
                width: 380; height: 540
                anchors.centerIn: parent
                // Fits the stage: shrinks on small screens, grows a little on large ones.
                scale: Math.min(1.45, stage.width / width, stage.height / height)

                Text {
                    x: 6; y: 2
                    text: "▲ FRONT"
                    color: Theme.off
                    font { pixelSize: 9; bold: true; letterSpacing: 1 }
                }

                // The car in its own coordinate system: front bumper at y ≈ 14, rear at y ≈ 466.
                Item {
                    id: body
                    y: 44
                    width: 380; height: 480

                    // ---------- light on the road (under the car) ----------
                    Beam { x0: 128; y0: 24; yEnd: -40; skew: -6; halfWidth: 12; spread: 14; tint: root.warmWhite; strength: 0.34; on: root.lowLeft }
                    Beam { x0: 252; y0: 24; yEnd: -40; skew: 6; halfWidth: 12; spread: 14; tint: root.warmWhite; strength: 0.34; on: root.lowRight }
                    Beam { x0: 128; y0: 24; yEnd: -44; skew: -2; halfWidth: 9; spread: 8; tint: root.coolWhite; strength: 0.5; on: root.highBeam }
                    Beam { x0: 252; y0: 24; yEnd: -44; skew: 2; halfWidth: 9; spread: 8; tint: root.coolWhite; strength: 0.5; on: root.highBeam }
                    // red glow on the road behind the car
                    Glow { cx: 122; cy: 462; r: root.braking ? 34 : 22; tint: root.tailRed; strength: root.braking ? 0.5 : 0.22; on: root.braking || root.parkLamps }
                    Glow { cx: 258; cy: 462; r: root.braking ? 34 : 22; tint: root.tailRed; strength: root.braking ? 0.5 : 0.22; on: root.braking || root.parkLamps }
                    Glow { cx: 190; cy: 470; r: 30; tint: "white"; strength: 0.3; on: root.reversing }

                    // ---------- exhaust ----------
                    Rectangle { x: 128; y: 462; width: 12; height: 8; radius: 3; color: "#2a2f37"; border.color: "#4a5262" }
                    Repeater {
                        id: exhaust
                        model: 3
                        // One cycle per puff; faster and denser with engine speed and load.
                        readonly property real load: Math.max(0, Math.min(1, root.v.throttle / 100))
                        readonly property int cycleMs: Math.round(Math.max(520, 1260 - (root.v.rpm - 800) * 0.12) / root.timeScale)
                        Rectangle {
                            id: puff
                            required property int index
                            x: 128; y: 468
                            width: 10; height: 10; radius: 5
                            color: "#c8d0dc"
                            opacity: 0
                            visible: root.v.engineRunning
                            SequentialAnimation {
                                running: root.decorate && root.v.engineRunning
                                loops: Animation.Infinite
                                PauseAnimation { duration: puff.index * exhaust.cycleMs / 3 }
                                ParallelAnimation {
                                    NumberAnimation { target: puff; property: "y"; from: 468; to: 496 + 8 * exhaust.load; duration: exhaust.cycleMs; easing.type: Easing.OutQuad }
                                    NumberAnimation { target: puff; property: "x"; from: 128; to: 118 + puff.index * 6; duration: exhaust.cycleMs; easing.type: Easing.OutSine }
                                    NumberAnimation { target: puff; property: "opacity"; from: 0.10 + 0.14 * exhaust.load; to: 0; duration: exhaust.cycleMs; easing.type: Easing.InQuad }
                                    NumberAnimation { target: puff; property: "scale"; from: 0.6; to: 1.8 + 0.8 * exhaust.load; duration: exhaust.cycleMs; easing.type: Easing.OutCubic }
                                }
                                PauseAnimation { duration: (2 - puff.index) * exhaust.cycleMs / 3 }
                            }
                        }
                    }

                    // ---------- wheels ----------
                    Wheel { x: 76; y: 72; bar: root.v.tyres[0] }
                    Wheel { x: 280; y: 72; bar: root.v.tyres[1] }
                    Wheel { x: 76; y: 344; bar: root.v.tyres[2] }
                    Wheel { x: 280; y: 344; bar: root.v.tyres[3] }
                    // park brake: the rear brakes glow while it is applied
                    Glow { cx: 88; cy: 376; r: 22; tint: root.tailRed; strength: 0.45; on: root.v.parkBrake }
                    Glow { cx: 292; cy: 376; r: 22; tint: root.tailRed; strength: 0.45; on: root.v.parkBrake }

                    // ---------- doors (swing open when ajar) ----------
                    Repeater {
                        model: [
                            { x: 95, y: 204, h: 66, left: true },
                            { x: 280, y: 204, h: 66, left: false },
                            { x: 95, y: 272, h: 62, left: true },
                            { x: 280, y: 272, h: 62, left: false }
                        ]
                        Rectangle {
                            id: door
                            required property int index
                            required property var modelData
                            readonly property bool open: root.doors[index] === true
                            y: modelData.y
                            width: 8; height: modelData.h
                            x: modelData.left ? modelData.x : modelData.x - 3
                            radius: 3
                            visible: rotation !== 0
                            color: "#252c37"
                            border.color: "#6b7690"
                            Rectangle { // door window
                                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 6 }
                                width: 3; height: parent.height * 0.55; radius: 1.5
                                color: "#33465c"
                            }
                            transformOrigin: Item.Top
                            rotation: open ? (modelData.left ? 38 : -38) : 0
                            Behavior on rotation { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }
                        }
                    }

                    // ---------- body shell ----------
                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: "#5a6478"
                            strokeWidth: 1.5
                            fillGradient: LinearGradient {
                                x1: 96; y1: 0; x2: 284; y2: 0
                                GradientStop { position: 0.0; color: "#171b22" }
                                GradientStop { position: 0.18; color: "#232a35" }
                                GradientStop { position: 0.5; color: "#2a3240" }
                                GradientStop { position: 0.82; color: "#232a35" }
                                GradientStop { position: 1.0; color: "#171b22" }
                            }
                            startX: 190; startY: 12
                            PathQuad { x: 252; y: 16; controlX: 224; controlY: 11 }
                            PathCubic { x: 282; y: 54; control1X: 272; control1Y: 19; control2X: 281; control2Y: 32 }
                            PathLine { x: 285; y: 148 }
                            PathQuad { x: 280; y: 176; controlX: 285; controlY: 166 }
                            PathLine { x: 280; y: 322 }
                            PathQuad { x: 285; y: 348; controlX: 285; controlY: 332 }
                            PathLine { x: 285; y: 430 }
                            PathCubic { x: 256; y: 463; control1X: 285; control1Y: 451; control2X: 275; control2Y: 461 }
                            PathQuad { x: 190; y: 467; controlX: 222; controlY: 467 }
                            PathQuad { x: 124; y: 463; controlX: 158; controlY: 467 }
                            PathCubic { x: 95; y: 430; control1X: 105; control1Y: 461; control2X: 95; control2Y: 451 }
                            PathLine { x: 95; y: 348 }
                            PathQuad { x: 100; y: 322; controlX: 95; controlY: 332 }
                            PathLine { x: 100; y: 176 }
                            PathQuad { x: 95; y: 148; controlX: 95; controlY: 166 }
                            PathLine { x: 98; y: 54 }
                            PathCubic { x: 128; y: 16; control1X: 99; control1Y: 32; control2X: 108; control2Y: 19 }
                            PathQuad { x: 190; y: 12; controlX: 156; controlY: 11 }
                        }
                        // hood creases, trunk line and door seams
                        ShapePath {
                            strokeColor: Qt.rgba(1, 1, 1, 0.07)
                            strokeWidth: 1
                            fillColor: "transparent"
                            startX: 146; startY: 34
                            PathQuad { x: 152; y: 166; controlX: 138; controlY: 100 }
                            PathMove { x: 234; y: 34 }
                            PathQuad { x: 228; y: 166; controlX: 242; controlY: 100 }
                            PathMove { x: 118; y: 380 }
                            PathQuad { x: 262; y: 380; controlX: 190; controlY: 372 }
                            PathMove { x: 100; y: 204 }
                            PathLine { x: 112; y: 204 }
                            PathMove { x: 268; y: 204 }
                            PathLine { x: 280; y: 204 }
                            PathMove { x: 100; y: 272 }
                            PathLine { x: 112; y: 272 }
                            PathMove { x: 268; y: 272 }
                            PathLine { x: 280; y: 272 }
                            PathMove { x: 100; y: 336 }
                            PathLine { x: 112; y: 336 }
                            PathMove { x: 268; y: 336 }
                            PathLine { x: 280; y: 336 }
                        }
                        // windscreen
                        ShapePath {
                            strokeColor: "#3c4658"
                            strokeWidth: 1
                            fillGradient: LinearGradient {
                                x1: 0; y1: 168; x2: 0; y2: 204
                                GradientStop { position: 0; color: "#33465c" }
                                GradientStop { position: 1; color: "#18212c" }
                            }
                            startX: 124; startY: 172
                            PathQuad { x: 256; y: 172; controlX: 190; controlY: 160 }
                            PathLine { x: 266; y: 204 }
                            PathQuad { x: 114; y: 204; controlX: 190; controlY: 198 }
                            PathLine { x: 124; y: 172 }
                        }
                        // rear window
                        ShapePath {
                            strokeColor: "#3c4658"
                            strokeWidth: 1
                            fillGradient: LinearGradient {
                                x1: 0; y1: 336; x2: 0; y2: 366
                                GradientStop { position: 0; color: "#18212c" }
                                GradientStop { position: 1; color: "#2b3b4e" }
                            }
                            startX: 116; startY: 338
                            PathQuad { x: 264; y: 338; controlX: 190; controlY: 344 }
                            PathLine { x: 254; y: 364 }
                            PathQuad { x: 126; y: 364; controlX: 190; controlY: 372 }
                            PathLine { x: 116; y: 338 }
                        }
                    }

                    // ---------- cabin (seen through the roof) ----------
                    Rectangle {
                        x: 112; y: 204; width: 156; height: 132; radius: 10
                        color: "#11151b"
                        border.color: "#2a313c"
                        // seats
                        Repeater {
                            model: [ { x: 14, y: 44, w: 46, h: 46 }, { x: 96, y: 44, w: 46, h: 46 }, { x: 14, y: 98, w: 128, h: 28 } ]
                            Rectangle {
                                required property var modelData
                                x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
                                radius: 8
                                color: "#171c24"
                                border.color: "#232a35"
                            }
                        }
                        // dome light while a door is open
                        Glow {
                            cx: 78; cy: 70; r: 70
                            tint: "#ffd892"
                            strength: 0.28
                            on: root.doors.indexOf(true) >= 0
                        }
                    }
                    // mirrors
                    Rectangle { x: 82; y: 196; width: 16; height: 10; radius: 4; color: "#232a35"; border.color: "#5a6478" }
                    Rectangle { x: 282; y: 196; width: 16; height: 10; radius: 4; color: "#232a35"; border.color: "#5a6478" }

                    // ---------- lamps on the body ----------
                    // headlamp units (low beam, high beam in the same housing)
                    Rectangle {
                        x: 108; y: 19; width: 40; height: 12; radius: 6; rotation: -10
                        color: "#151a21"; border.color: "#4a5262"; antialiasing: true
                    }
                    Rectangle {
                        x: 232; y: 19; width: 40; height: 12; radius: 6; rotation: 10
                        color: "#151a21"; border.color: "#4a5262"; antialiasing: true
                    }
                    LampLens { x: 126; y: 20; width: 16; height: 8; rotation: -10; tint: root.highBeam ? root.coolWhite : root.warmWhite; on: root.lowLeft || root.highBeam; haloRadius: 16 }
                    LampLens { x: 238; y: 20; width: 16; height: 8; rotation: 10; tint: root.highBeam ? root.coolWhite : root.warmWhite; on: root.lowRight || root.highBeam; haloRadius: 16 }
                    // position lamps (daytime strip)
                    LampLens { x: 112; y: 25; width: 12; height: 4; rotation: -10; tint: root.coolWhite; on: root.parkLamps; level: 0.7; haloRadius: 9 }
                    LampLens { x: 256; y: 25; width: 12; height: 4; rotation: 10; tint: root.coolWhite; on: root.parkLamps; level: 0.7; haloRadius: 9 }
                    // front indicators, mirror repeaters, rear indicators
                    LampLens { x: 100; y: 38; width: 8; height: 10; offColor: "#3a3020"; tint: root.amber; on: root.v.turnLeftLamp; haloRadius: 16 }
                    LampLens { x: 272; y: 38; width: 8; height: 10; offColor: "#3a3020"; tint: root.amber; on: root.v.turnRightLamp; haloRadius: 16 }
                    LampLens { x: 83; y: 198; width: 6; height: 6; offColor: "#3a3020"; tint: root.amber; on: root.v.turnLeftLamp; haloRadius: 10 }
                    LampLens { x: 291; y: 198; width: 6; height: 6; offColor: "#3a3020"; tint: root.amber; on: root.v.turnRightLamp; haloRadius: 10 }
                    LampLens { x: 100; y: 440; width: 8; height: 10; offColor: "#3a3020"; tint: root.amber; on: root.v.turnLeftLamp; haloRadius: 16 }
                    LampLens { x: 272; y: 440; width: 8; height: 10; offColor: "#3a3020"; tint: root.amber; on: root.v.turnRightLamp; haloRadius: 16 }
                    // tail / brake lamps
                    LampLens {
                        x: 110; y: 452; width: 34; height: 7; rotation: 8
                        offColor: "#3a1a1f"; tint: root.tailRed
                        on: root.parkLamps || root.braking
                        level: root.braking ? 1.0 : 0.45
                        haloRadius: root.braking ? 18 : 12
                    }
                    LampLens {
                        x: 236; y: 452; width: 34; height: 7; rotation: -8
                        offColor: "#3a1a1f"; tint: root.tailRed
                        on: root.parkLamps || root.braking
                        level: root.braking ? 1.0 : 0.45
                        haloRadius: root.braking ? 18 : 12
                    }
                    // third brake light on the rear window
                    LampLens { x: 168; y: 361; width: 44; height: 4; offColor: "#2c1a1e"; tint: root.tailRed; on: root.braking; haloRadius: 12 }
                    // reversing lamps
                    LampLens { x: 150; y: 458; width: 12; height: 5; offColor: "#30343c"; tint: "white"; on: root.reversing; haloRadius: 12 }
                    LampLens { x: 218; y: 458; width: 12; height: 5; offColor: "#30343c"; tint: "white"; on: root.reversing; haloRadius: 12 }

                    // ---------- CAN bus: backbone with stubs ----------
                    Wire { x: 189; y: 96; width: 2; height: 204 }      // backbone
                    Wire { x: 172; y: 139; width: 18; height: 2 }      // ABS
                    Wire { x: 190; y: 163; width: 18; height: 2 }      // TCM
                    Wire { x: 136; y: 262; width: 54; height: 2 }      // DLC
                    // frames travelling along the backbone while the bus is busy
                    Repeater {
                        id: frames
                        model: 3
                        readonly property int travelMs: Math.round(900 / root.timeScale)
                        // A busier bus gives brighter frames (at least visible at idle traffic).
                        readonly property real brightness: Math.max(0.55, Math.min(1, 0.45 + root.v.busLoad * 3))
                        Rectangle {
                            id: packet
                            required property int index
                            readonly property bool active: root.decorate && !root.v.busFailure && root.v.busLoad > 0.005
                            x: 187; y: 96
                            width: 6; height: 6; radius: 3
                            color: "#ffe28a"
                            opacity: 0
                            visible: active
                            Glow { cx: 3; cy: 3; r: 7; tint: "#ffd75e"; strength: 0.7 * frames.brightness; on: true }
                            SequentialAnimation {
                                running: packet.active
                                loops: Animation.Infinite
                                PauseAnimation { duration: packet.index * frames.travelMs / 3 }
                                ParallelAnimation {
                                    NumberAnimation { target: packet; property: "y"; from: 96; to: 296; duration: frames.travelMs; easing.type: Easing.InOutSine }
                                    SequentialAnimation {
                                        NumberAnimation { target: packet; property: "opacity"; from: 0; to: frames.brightness; duration: frames.travelMs / 6 }
                                        PauseAnimation { duration: frames.travelMs * 2 / 3 }
                                        NumberAnimation { target: packet; property: "opacity"; to: 0; duration: frames.travelMs / 6 }
                                    }
                                }
                                PauseAnimation { duration: (2 - packet.index) * frames.travelMs / 3 }
                            }
                        }
                    }
                    Rectangle {
                        x: 196; y: 274
                        width: canLabel.implicitWidth + 8; height: 13; radius: 3
                        color: "#cc0d1015"
                        border.color: root.v.busFailure ? Theme.error : "#5c4b16"
                        Text {
                            id: canLabel
                            anchors.centerIn: parent
                            text: root.v.busFailure ? "CAN  ✕ SHORT" : "CAN 500k"
                            color: root.v.busFailure ? Theme.error : "#e0b93a"
                            font { pixelSize: 8; bold: true }
                        }
                    }
                    MouseArea {
                        x: 182; y: 244; width: 76; height: 46
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.targetPicked("can_bus")
                    }

                    // ---------- control units ----------
                    EcuBox { name: "ECM"; x: 144; y: 58; width: 92; height: 38 }
                    EcuBox { name: "ABS"; x: 112; y: 124; width: 60; height: 32 }
                    EcuBox { name: "TCM"; x: 208; y: 148; width: 60; height: 32 }
                    EcuBox { name: "IPC"; x: 146; y: 214; width: 88; height: 26 }
                    EcuBox { name: "BCM"; x: 152; y: 300; width: 76; height: 30 }

                    // OBD connector (scan tool)
                    Rectangle {
                        x: 106; y: 252; width: 30; height: 22; radius: 4
                        color: "#1c2a3a"; border.color: Theme.accent
                        Text { anchors.centerIn: parent; text: "OBD"; color: Theme.accent; font { pixelSize: 8; bold: true } }
                    }

                    // ---------- sensors and components ----------
                    Repeater {
                        model: root.spots
                        Hotspot {
                            required property var modelData
                            readonly property var place: root.spotPlaces[modelData.id]
                            targetId: modelData.id
                            label: modelData.label || modelData.id.toUpperCase()
                            name: modelData.name
                            x: place.x; y: place.y
                            labelLeft: place.left === true
                        }
                    }
                }
            }

            // Start prompt: a new vehicle waits, parked with the ignition off, until the user runs it.
            Rectangle {
                id: prompt
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 300)
                implicitHeight: promptColumn.implicitHeight + 28
                radius: 10
                color: Qt.rgba(0.06, 0.07, 0.09, 0.9)
                border.color: Qt.rgba(0.29, 0.66, 1, 0.55)
                z: 20
                opacity: root.waiting ? 1 : 0
                visible: opacity > 0.01
                scale: root.waiting ? 1 : 0.96
                Behavior on opacity { NumberAnimation { duration: App.reducedMotion ? 0 : 220; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: App.reducedMotion ? 0 : 220; easing.type: Easing.OutCubic } }
                ColumnLayout {
                    id: promptColumn
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "Simulation paused"
                        color: Theme.text
                        font { pixelSize: 15; bold: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "The car is parked with the ignition off and the clock stands at 0 s. Start the simulation, then switch the ignition on — or pick a lesson."
                        color: Theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: "▶  Run   (Space)"
                        highlighted: true
                        onClicked: App.running = true
                    }
                }
            }
        }

        // --- what the sensors really see vs. what the ECU measures ---
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: "SENSOR"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 66; Layout.leftMargin: 2 }
                Text { text: "REAL"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 40 }
                Text { text: "ECU MEASURES"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 74 }
                Text { text: "PIN"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 44 }
                Text { text: "STATE"; color: Theme.textDim; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 40; Layout.maximumWidth: 84; Layout.rightMargin: 2 }
            }
            Repeater {
                // One row per fitted sensor, in the order the simulation lists them. sensorIds only
                // changes with the equipment: the rows are not rebuilt on every refresh.
                model: App.sensorIds
                Rectangle {
                    id: cell
                    required property string modelData
                    required property int index
                    readonly property var s: root.sensorMap[modelData]
                    Layout.fillWidth: true
                    implicitHeight: 16
                    radius: 3
                    color: cellArea.containsMouse ? Theme.panelAlt : (cell.s && cell.s.faulted ? Qt.rgba(1, 0.3, 0.37, 0.08) : "transparent")
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        anchors.rightMargin: 2
                        spacing: 6
                        Text {
                            Layout.minimumWidth: 66
                            text: cell.s ? cell.s.shortName : ""
                            color: cell.s && cell.s.faulted ? Theme.error : Theme.text
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: cell.s ? cell.s.trueValue.toFixed(1) : ""
                            color: Theme.textDim
                            font { family: Theme.mono; pixelSize: 11 }
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 40
                        }
                        Text {
                            text: cell.s ? cell.s.value.toFixed(1) + " " + cell.s.unit : ""
                            color: cell.s && Math.abs(cell.s.value - cell.s.trueValue) > 0.6 ? Theme.error : Theme.text
                            font { family: Theme.mono; pixelSize: 11 }
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 74
                        }
                        Text {
                            text: cell.s && !cell.s.digital ? cell.s.voltage.toFixed(2) + " V" : "pulse"
                            color: Theme.textDim
                            font { family: Theme.mono; pixelSize: 11 }
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 44
                        }
                        Text {
                            text: cell.s ? (cell.s.faulted ? cell.s.fault : "ok") : ""
                            color: cell.s && cell.s.faulted ? Theme.error : Theme.ok
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 40
                            Layout.maximumWidth: 84
                        }
                    }
                    MouseArea {
                        id: cellArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.targetPicked(cell.modelData)
                    }
                }
            }
        }
    }
}
