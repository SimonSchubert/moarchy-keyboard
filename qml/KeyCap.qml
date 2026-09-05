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

    // Typed, not `var`, and that is the point rather than a tidy-up. qmllint
    // can resolve a field on a keyspec and cannot resolve one on a var; on this
    // program an unresolvable name becomes undefined, an undefined assigned to
    // a color is #000000, and the result is a black glyph on a black key with
    // nothing logged. Every default below was applied once by LayoutParser, so
    // there is no `!== undefined` left to get wrong.
    required property keyspec spec
    required property bool shifted
    required property string iconFamily
    property bool pressed: false

    // The three modifier states arrive separately rather than as one object.
    // A single `modifierStates` map was rebuilt on every modifier press, so
    // every key on the panel saw a changed property and re-evaluated this
    // binding and the four below it. Read one at a time, a character key's
    // binding returns at the first line and captures no dependency on any of
    // them at all -- QML records what a binding actually read -- so pressing
    // Ctrl now re-evaluates the Ctrl key, not all forty-five.
    required property int shiftState
    required property int ctrlState
    required property int altState

    // 0 off, 1 latched for the next key only, 2 locked until pressed again.
    // Derived rather than assigned, because nothing was assigning it: a latched
    // Shift looked exactly like an idle one, which makes the difference between
    // "the next letter is capital" and "every letter is capital" invisible
    // (AC 34).
    readonly property int modifierState: {
        if (root.keyType !== KeyType.Modifier)
            return 0
        switch (root.spec.modifier) {
        case KeyModifier.Shift: return root.shiftState
        case KeyModifier.Ctrl:  return root.ctrlState
        case KeyModifier.Alt:   return root.altState
        }
        return 0
    }
    readonly property bool latched: modifierState > 0
    readonly property bool locked: modifierState === 2

    readonly property int keyType: root.spec.type
    readonly property bool isAccent: root.spec.accent
    readonly property bool repeats: root.spec.repeats
    readonly property list<string> alternates: root.spec.alt
    readonly property bool hasAlternates: root.spec.hasAlternates

    // A key may ask to be drawn from an icon font. Whether it CAN be is a
    // runtime question -- the font may not have loaded -- so the spec carries
    // the request and this carries the answer. Without the font the key falls
    // back to a text label rather than to a missing-glyph box, which is the
    // difference between a keyboard that looks plainer than intended and one
    // that looks broken.
    readonly property bool usesIcon: root.spec.iconFont && root.iconFamily !== ""

    readonly property string label:
        root.usesIcon && root.spec.glyph !== ""
            ? root.spec.glyph
            : (root.shifted ? root.spec.shiftedLabel : root.spec.label)

    readonly property color fill: {
        if (pressed || locked || isAccent)
            return Colors.accent
        if (keyType === KeyType.Character)
            return Colors.keyFill
        return Colors.modifierFill
    }

    readonly property color textColor: {
        if (pressed || locked || isAccent)
            return Colors.accentText
        if (latched)
            return Colors.accent          // one-shot: tinted, not filled
        return keyType === KeyType.Character ? Colors.keyText : Colors.modifierText
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
                            : (root.keyType === KeyType.Character ? 19 : 13)
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
