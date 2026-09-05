import QtQuick
import moarchy

// One key.
//
// The Item fills its slot in the row exactly; the visible Rectangle is inset
// inside it. That split is what makes AC 29 true: the gaps you can see between
// keys belong to a key as far as touch is concerned, so there is nowhere on the
// panel you can land and hit nothing. Drawing the gap as a margin on the
// *visual* rather than as spacing in the *layout* costs nothing and removes a
// whole class of "it ignored my tap" complaints.
Item {
    id: root

    required property var spec
    required property bool shifted
    required property var modifierStates
    required property string iconFamily
    property bool pressed: false

    // 0 off, 1 latched for the next key only, 2 locked until pressed again.
    // Derived rather than assigned, because nothing was assigning it: a latched
    // Shift looked exactly like an idle one, which makes the difference between
    // "the next letter is capital" and "every letter is capital" invisible
    // (AC 34).
    readonly property int modifierState: {
        if (keyType !== "modifier" || spec.modifier === undefined)
            return 0
        var state = modifierStates[spec.modifier]
        return state === undefined ? 0 : state
    }
    readonly property bool latched: modifierState > 0
    readonly property bool locked: modifierState === 2

    readonly property string keyType: spec.type !== undefined ? spec.type : "character"
    readonly property bool isSpace: keyType === "space"
    readonly property bool isAccent: spec.accent === true
    readonly property bool repeats: spec.repeats === true
    readonly property var alternates: spec.alt !== undefined ? spec.alt : []
    readonly property bool hasAlternates: alternates.length > 0

    // A key may ask to be drawn from an icon font. If that font is not
    // available the key falls back to a text label rather than to a
    // missing-glyph box, which is the difference between a keyboard that looks
    // plainer than intended and one that looks broken.
    readonly property bool usesIcon: spec.font === "omarchy" && root.iconFamily !== ""

    readonly property string label: {
        if (usesIcon && spec.glyph !== undefined)
            return spec.glyph
        if (spec.label !== undefined)
            return spec.label
        if (spec.text !== undefined)
            return root.shifted ? spec.text.toUpperCase() : spec.text
        return ""
    }

    readonly property color fill: {
        if (pressed || locked || isAccent)
            return Colors.accent
        if (keyType === "character")
            return Colors.keyFill
        return Colors.modifierFill
    }

    readonly property color textColor: {
        if (pressed || locked || isAccent)
            return Colors.accentText
        if (latched)
            return Colors.accent          // one-shot: tinted, not filled
        return keyType === "character" ? Colors.keyText : Colors.modifierText
    }

    Rectangle {
        id: cap
        anchors.fill: parent
        anchors.margins: 2
        radius: 5
        color: root.fill
        // A one-shot latch is an outline; a lock is a fill. Two different
        // states must not look the same.
        border.width: root.latched && !root.locked ? 2 : 0
        border.color: Colors.accent
        // No Behavior on colour, in either direction.
        //
        // The tempting version is `Behavior on color { enabled: !root.pressed }`
        // so releases fade and presses do not. That relies on `enabled` being
        // re-evaluated before the colour binding on the same change, and QML
        // guarantees no order between two bindings on one event -- so a press
        // would animate some of the time and not others. AC 33 gives the entire
        // budget to the first frame, and an intermittent 90 ms fade is exactly
        // the kind of "feels laggy sometimes" that never gets diagnosed.
        //
        // A key that lights instantly and unlights instantly is also what a real
        // keyboard does.

        Text {
            anchors.centerIn: parent
            text: root.label
            color: root.textColor
            font.pixelSize: root.usesIcon ? 20
                            : (root.keyType === "character" ? 19 : 13)
            font.family: root.usesIcon ? root.iconFamily : "sans-serif"
            renderType: Text.NativeRendering
        }

        // The long-press hint, top-right, quiet. Suppressed in password fields
        // along with the popup itself.
        Text {
            visible: root.hasAlternates && !Router.sensitive
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 2
            anchors.rightMargin: 4
            text: root.alternates.length > 0 ? root.alternates[0] : ""
            color: Colors.keyHint
            font.pixelSize: 10
            renderType: Text.NativeRendering
        }
    }
}
