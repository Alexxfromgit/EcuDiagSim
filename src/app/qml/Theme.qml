pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#12151a"
    readonly property color panel: "#1a1e25"
    readonly property color panelAlt: "#222731"
    readonly property color inset: "#0e1014"
    readonly property color border: "#2e3440"
    readonly property color text: "#e4e8ee"
    readonly property color textDim: "#8a93a3"
    readonly property color accent: "#4aa8ff"
    readonly property color ok: "#3ccf8e"
    readonly property color warn: "#f5a524"
    readonly property color error: "#ff4d5e"
    readonly property color violet: "#a78bfa"
    readonly property color off: "#3a404c"

    readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : (Qt.platform.os === "osx" ? "Menlo" : "DejaVu Sans Mono")
    readonly property int radius: 6

    function ecuColor(state) {
        switch (state) {
        case "ok": return ok
        case "stored": return warn
        case "fault": return error
        case "busoff": return violet
        default: return off
        }
    }
    function ecuStateText(state) {
        switch (state) {
        case "ok": return "running, no faults"
        case "stored": return "running, DTCs stored"
        case "fault": return "running, fault active"
        case "busoff": return "bus-off"
        case "nopower": return "no power supply"
        case "sleep": return "asleep (ignition off)"
        default: return state
        }
    }
}
