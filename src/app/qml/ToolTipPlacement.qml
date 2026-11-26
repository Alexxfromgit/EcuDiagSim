import QtQuick
import QtQuick.Controls

// Keeps the shared attached tooltip (ToolTip.visible / ToolTip.text on any item) off the item it
// describes. A tooltip popup swallows the mouse, so a tooltip drawn over its item - a sensor
// hotspot on the scaled vehicle drawing, a lamp, an item near a window edge - took the click
// meant for the item.
//
// Qt's own placement can't be trusted for this: it scales the tooltip's offset with a scaled
// parent (the vehicle drawing) but not the tooltip itself, and then pushes the tooltip back into
// the window on top of the item when its maths say it doesn't fit. So the fitting is done here,
// in window coordinates, and Qt's push-back is switched off (margins -1). The tooltip goes on the
// first side of its item where it fits whole - above, below, right, left - and is slid along
// that side to stay inside the window.
Item {
    id: placement

    readonly property ToolTip tip: ToolTip.toolTip
    readonly property int gap: 4
    readonly property int edge: 6 // distance kept from the window border

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(v, hi)) }

    function place() {
        const t = tip
        const p = t ? t.parent : null
        const win = p ? p.Window.window : null
        if (!p || !win || p.width <= 0 || p.height <= 0)
            return
        t.margins = -1

        // The item and the room around it, in window coordinates.
        const a = p.mapToItem(null, 0, 0), b = p.mapToItem(null, p.width, p.height)
        const left = Math.min(a.x, b.x), right = Math.max(a.x, b.x)
        const top = Math.min(a.y, b.y), bottom = Math.max(a.y, b.y)
        const W = win.width, H = win.height
        const w = t.implicitWidth, h = t.implicitHeight
        const roomAbove = top - gap - edge, roomBelow = H - edge - bottom - gap
        const roomLeft = left - gap - edge, roomRight = W - edge - right - gap

        const centreX = clamp((left + right - w) / 2, edge, W - edge - w)
        const centreY = clamp((top + bottom - h) / 2, edge, H - edge - h)
        let x, y
        if (h <= roomAbove) {
            x = centreX; y = top - gap - h
        } else if (h <= roomBelow) {
            x = centreX; y = bottom + gap
        } else if (w <= roomRight) {
            x = right + gap; y = centreY
        } else if (w <= roomLeft) {
            x = left - gap - w; y = centreY
        } else if (roomBelow >= roomAbove) { // fits nowhere whole: the larger side
            x = centreX; y = bottom + gap
        } else {
            x = centreX; y = top - gap - h
        }

        // Popup x / y are in the parent's coordinates, scale included.
        const local = p.mapFromItem(null, x, y)
        t.x = local.x
        t.y = local.y
    }

    Connections {
        target: placement.tip
        function onAboutToShow() { placement.place() }
        // Hovering straight from one item to the next reuses the open tooltip, and live texts
        // (sensor values, trip computer) change its size while it is shown.
        function onParentChanged() { Qt.callLater(placement.place) }
        function onImplicitWidthChanged() { Qt.callLater(placement.place) }
        function onImplicitHeightChanged() { Qt.callLater(placement.place) }
    }
}
