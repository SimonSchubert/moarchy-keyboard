import QtQuick
import moarchy

// The panel's root. Deliberately thin: everything interesting is in Keyboard.qml.
//
// `visible` is driven from C++ (Panel::applyVisibility) rather than bound here.
// Retracting the keyboard must not destroy the layer surface, so the surface
// stays mapped and this item stops drawing instead -- see the comment at the top
// of src/panel.h for why that distinction is the whole ballgame.
Rectangle {
    id: root

    color: Colors.panelBackground

    // Omarchy's icon font, loaded once here rather than per key. Its family
    // name is read from the loader rather than assumed to be "omarchy", so a
    // renamed font still works, and an empty string when it did not load lets
    // each key fall back to a text label instead of a missing-glyph box.
    FontLoader {
        id: iconFont
        source: Layouts.iconFontUrl
    }

    // A hairline against the app above, so the keyboard reads as a separate
    // surface even when its background matches the window behind it.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Colors.separator
    }

    // Edge to edge, no side margins.
    //
    // An earlier version inset the left by 20px to leave mobileomarchy's
    // back-edge gesture its band, then matched it on the right so the asymmetry
    // did not read as a bug. Both were wrong: it gave up 40px of a 360px screen
    // so that a gesture could operate on top of a keyboard, and while the
    // keyboard is up the left edge should type. The back gesture is still
    // available across the whole app area above it.
    Keyboard {
        anchors.fill: parent
        anchors.topMargin: 1
        iconFamily: iconFont.status === FontLoader.Ready ? iconFont.font.family : ""
    }
}
