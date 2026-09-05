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

    Keyboard {
        anchors.fill: parent
        anchors.topMargin: 1
    }
}
