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

    required property keyrow row
    required property real unit
    required property bool shifted
    // 0 off, 1 latched for one key, 2 locked. Three ints rather than one map,
    // so that a modifier press only dirties the keys that care -- see the note
    // on them in KeyCap.qml.
    required property int shiftState
    required property int ctrlState
    required property int altState
    required property string iconFamily

    // The row's total width in key-widths used to be computed here, by an
    // interpreted loop over every key, on every layout switch -- into a
    // property that nothing ever read. It is `row.units` now, measured once by
    // the parser, and read by nobody, which is at least honest.

    function clearPressed() {
        for (var i = 0; i < strip.children.length; ++i) {
            var cap = strip.children[i] as KeyCap
            if (cap)
                cap.pressed = false
        }
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
            model: root.row.keys
            delegate: KeyCap {
                required property keyspec modelData
                spec: modelData
                shifted: root.shifted
                shiftState: root.shiftState
                ctrlState: root.ctrlState
                altState: root.altState
                iconFamily: root.iconFamily
                width: root.unit * modelData.width
                height: strip.height
            }
        }
    }
}
