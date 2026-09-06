import QtQuick
import moarchy

// The panel's root.
//
// One surface serves both states -- the keyboard and the restore handle --
// because the surface must never be unmapped once created (see the note at the
// top of src/panel.h). So the root is a transparent Item and each state paints
// its own background, rather than the root painting one that would show
// through behind the handle.
//
// The two are one bool apart on purpose (AC 49): whenever the keyboard is not
// on screen the handle is, so there is no arrangement of events that leaves the
// phone with neither a keyboard nor a way to ask for one.
Item {
    id: root

    // Omarchy's icon font, loaded once here rather than per key. Its family
    // name is read from the loader rather than assumed to be "omarchy", so a
    // renamed font still works, and an empty string when it did not load lets
    // each key fall back to a text label instead of a missing-glyph box.
    FontLoader {
        id: iconFont
        source: Layouts.iconFontUrl
    }

    // ------------------------------------------------------------- keyboard
    Rectangle {
        anchors.fill: parent
        visible: Panel.shown
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

        // Edge to edge, no side margins. An earlier version inset both sides to
        // leave the back-edge gesture its band, which gave up 40px of a 360px
        // screen so a gesture could operate on top of a keyboard.
        // Inset out of the gesture strip's band at the bottom. The surface now
        // runs under the strip so its background reaches the screen edge (see
        // kDefaultStripInset in panel.cpp); the keys must not follow it there,
        // or the bottom row sits under the home pill, which is on Overlay and
        // takes those touches first.
        Keyboard {
            anchors.fill: parent
            anchors.topMargin: 1
            anchors.bottomMargin: Panel.stripInset
            iconFamily: iconFont.status === FontLoader.Ready ? iconFont.font.family : ""
        }
    }

    // --------------------------------------------------------------- handle
    RestoreHandle {
        id: handle
        visible: !Panel.shown

        // Bottom right, clear of both gesture bands. The strip's band is now
        // *inside* this surface rather than below it, so the margin has to
        // carry it -- at a bare 10 the handle would sit under the home pill,
        // and the handle is the only way back from a dismissed keyboard.
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        anchors.bottomMargin: 10 + Panel.stripInset

        // The input region is set from this, so that the rest of the surface --
        // still mapped, still transparent -- does not swallow touches meant for
        // the app underneath.
        function report() {
            Panel.setHandleRect(x, y, width, height)
        }
        onXChanged: report()
        onYChanged: report()
        onWidthChanged: report()
        onHeightChanged: report()
        Component.onCompleted: report()
    }
}
