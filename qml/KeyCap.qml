import QtQuick

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
    property bool pressed: false
    property bool latched: false

    readonly property string keyType: spec.type !== undefined ? spec.type : "character"
    readonly property bool isSpace: keyType === "space"
    readonly property bool isAccent: spec.accent === true
    readonly property bool repeats: spec.repeats === true
    readonly property var alternates: spec.alt !== undefined ? spec.alt : []
    readonly property bool hasAlternates: alternates.length > 0

    readonly property string label: {
        if (spec.label !== undefined)
            return spec.label
        if (spec.text !== undefined)
            return root.shifted ? spec.text.toUpperCase() : spec.text
        return ""
    }

    readonly property color fill: {
        if (pressed)
            return Colors.accent
        if (latched)
            return Colors.accent
        if (isAccent)
            return Colors.accent
        if (keyType === "character")
            return Colors.keyFill
        return Colors.modifierFill
    }

    readonly property color textColor: {
        if (pressed || latched || isAccent)
            return Colors.accentText
        return keyType === "character" ? Colors.keyText : Colors.modifierText
    }

    Rectangle {
        id: cap
        anchors.fill: parent
        anchors.margins: 2
        radius: 5
        color: root.fill
        // No animation on press: a colour transition, however short, is one more
        // thing between the finger and the feedback, and AC 33 gives the whole
        // budget to the first frame. Release fades, press does not.
        Behavior on color {
            enabled: !root.pressed
            ColorAnimation { duration: 90 }
        }

        Text {
            anchors.centerIn: parent
            text: root.label
            color: root.textColor
            font.pixelSize: root.keyType === "character" ? 19 : 13
            font.family: "sans-serif"
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
