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

    // A hairline against the app above, so the keyboard reads as a separate
    // surface even when its background matches the window behind it.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Colors.separator
    }

    // The left band belongs to mobileomarchy's back-edge gesture, and the
    // panel's input region excludes it (see kDefaultBackEdgeInset in
    // src/panel.cpp). The keys are inset by the same amount so that none of
    // them sits in a band where touches are deliberately ignored -- a key that
    // looks 36px wide and responds across 20 is worse than a narrower key that
    // works everywhere it looks like it should.
    //
    // 20 rather than 16 because mobileomarchy declares the band as
    // Style.space(16), and Style.space rounds a scaled value -- about 18 at the
    // default scale. Must match kDefaultBackEdgeInset in src/panel.cpp.
    readonly property int backEdgeInset: 20

    // Loaded once here rather than per key. Its family name is whatever the
    // font declares -- read from the loader rather than assumed to be
    // "omarchy", so a renamed font still works.
    FontLoader {
        id: iconFont
        source: Layouts.iconFontUrl
    }

    Keyboard {
        iconFamily: iconFont.status === FontLoader.Ready ? iconFont.font.family : ""
        anchors.fill: parent
        anchors.topMargin: 1

        // Matched on both sides. The left margin is load-bearing and the right
        // one is not, but a keyboard indented on one side only reads as a bug
        // rather than as padding -- which is exactly how it was reported. The
        // right band still takes touches: rows clamp a tap into the nearest key,
        // so the margin is dead space to look at and live space to hit.
        anchors.leftMargin: root.backEdgeInset
        anchors.rightMargin: root.backEdgeInset
    }
}
