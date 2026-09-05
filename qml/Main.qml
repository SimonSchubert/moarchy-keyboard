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
    // panel's input region excludes it (see kBackEdgeInset in src/panel.cpp).
    // The keys are inset by the same amount so that none of them sits in a band
    // where touches are deliberately ignored -- without this the leftmost
    // column would look 36px wide and respond across 20 of them, which is worse
    // than a narrower key that works everywhere it looks like it should.
    //
    // 20 rather than 16 because mobileomarchy declares the band as
    // Style.space(16), and Style.space rounds a scaled value -- about 18 at the
    // default scale. Must match kDefaultBackEdgeInset in src/panel.cpp.
    readonly property int backEdgeInset: 20

    Keyboard {
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.leftMargin: root.backEdgeInset
    }
}
