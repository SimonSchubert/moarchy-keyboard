pragma ComponentBehavior: Bound

import QtQuick
import moarchy

// A row of keys, full panel width, with the keys centred inside it.
//
// Rows are not all the same total width -- the home row is nine keys against
// the top row's ten -- so a narrower row leaves space at each end. That space is
// still live: keyAt() clamps into the strip before hit-testing, which hands the
// margin to the outer key rather than leaving a dead column down each edge of
// the keyboard (AC 29).
Item {
    id: root

    required property var keys
    required property real unit
    required property bool shifted
    // { shift, ctrl, alt } -> 0 off, 1 latched for one key, 2 locked.
    required property var modifierStates
    required property string iconFamily

    readonly property real strips: {
        var total = 0
        for (var i = 0; i < keys.length; ++i)
            total += keys[i].width !== undefined ? keys[i].width : 1
        return total
    }

    function keyAt(x, y) {
        if (strip.width <= 0)
            return null
        var clamped = Math.max(strip.x + 1, Math.min(strip.x + strip.width - 1, x))
        var local = clamped - strip.x
        // Vertically anything inside the row belongs to that row's keys, so the
        // y passed in is deliberately ignored in favour of the strip's middle.
        return strip.childAt(local, strip.height / 2)
    }

    Row {
        id: strip
        anchors.horizontalCenter: parent.horizontalCenter
        height: parent.height
        spacing: 0

        Repeater {
            model: root.keys
            delegate: KeyCap {
                required property var modelData
                spec: modelData
                shifted: root.shifted
                modifierStates: root.modifierStates
                iconFamily: root.iconFamily
                width: root.unit * (modelData.width !== undefined ? modelData.width : 1)
                height: strip.height
            }
        }
    }
}
