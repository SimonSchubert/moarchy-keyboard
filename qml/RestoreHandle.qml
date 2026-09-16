pragma ComponentBehavior: Bound

import QtQuick
import moarchy

// The tab that brings the keyboard back.
//
// It exists because of a gap in the platform, not because a keyboard wants
// decoration. Dismissing the keyboard by gesture leaves the text field focused,
// and tapping that field again emits nothing at all -- measured with
// WAYLAND_DEBUG: zero input-method traffic, because nothing about the client's
// text state changed. So there is no event to wake up on, and without something
// to tap the keyboard is unreachable until focus moves elsewhere.
//
// It was shown in exactly that state at first -- dismissed by hand, with a text
// input still active -- on the grounds that any other time it would be clutter.
// That had the trade backwards. Every other way of ending up without a keyboard
// leaves nothing to tap just the same: an app that never advertises text input,
// a terminal that holds it the whole time it is focused, a dismissal that
// arrives before the state which was supposed to justify the handle. A phone
// that cannot ask for a keyboard is worse than a pill in the corner of the
// screen, so this is drawn whenever the keyboard is not (AC 49) and the whole
// decision is one bool.
//
// And it moves (§4.10). Wherever a pill is fixed it covers something -- bottom
// right is where a chat app puts Send -- so a drag slides it up and down the
// right edge, and a tap is what is left: a press that lifts without travelling.
Item {
    id: root

    readonly property int pillWidth: 54
    readonly property int pillHeight: 30

    // How far a press may wander and still be a tap (AC 55), in logical px.
    // Past it the press is a drag for the rest of its life, even if the finger
    // comes back: a person who moved the handle and put it back where it was
    // did not ask for the keyboard.
    readonly property int dragThreshold: 10

    implicitWidth: pillWidth
    implicitHeight: pillHeight

    Rectangle {
        id: pill
        anchors.fill: parent
        // style.md D1's tile radius, capped at half the short side (AC 59) --
        // moarchy's Ui.radiusOn, which the shell's Pill, Fab and IconButton go
        // through. So `large` stays the capsule this always was, `modest`
        // rounds like the shell's tiles, and `square` is square.
        radius: Math.min(Chrome.tile, height / 2)

        // Reads as a raised control rather than as a key: the key fill would
        // make it look like a stray key floating over the app.
        color: Colors.modifierFill
        border.width: 1
        border.color: Colors.blend(Colors.modifierText, Colors.modifierFill, 0.22)

        // Pressed state, because a control with no feedback feels broken on a
        // touchscreen even when it works.
        scale: touch.held ? 0.94 : 1.0
        Behavior on scale { NumberAnimation { duration: 70 } }

        // A miniature keyboard, drawn rather than set as a glyph: the obvious
        // character for this is U+2328, which is missing from most of the fonts
        // on this phone and renders as an empty box. Rectangles always draw.
        Item {
            id: glyph
            width: 22
            height: 14
            anchors.centerIn: parent

            Rectangle {
                anchors.fill: parent
                radius: 3
                color: "transparent"
                border.width: 1.5
                border.color: Colors.modifierText
            }

            // Two rows of keys and a spacebar, at the smallest size that still
            // reads as a keyboard rather than as noise.
            Column {
                anchors.centerIn: parent
                spacing: 2

                Repeater {
                    model: 2
                    delegate: Row {
                        spacing: 2
                        Repeater {
                            model: 4
                            delegate: Rectangle {
                                width: 2.5
                                height: 2
                                radius: 0.5
                                color: Colors.modifierText
                            }
                        }
                    }
                }

                Rectangle {
                    width: 10
                    height: 2
                    radius: 1
                    color: Colors.modifierText
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    // One finger. The handle does not move inside this surface -- the surface
    // moves on screen, through Panel -- so a point's y stays in the frame it
    // was pressed in, and startY - y is how far the finger has gone. sway keeps
    // reporting a touch relative to where its surface was at the press
    // (seatop_down), which is what keeps that true while the surface slides.
    MultiPointTouchArea {
        id: touch
        anchors.fill: parent
        mouseEnabled: true
        maximumTouchPoints: 1

        // MultiPointTouchArea has no `pressed` of its own; the scale above read
        // one for as long as this file has existed and always got undefined.
        property bool held: false
        property bool dragging: false
        property int startOffset: 0

        onPressed: function (points) {
            held = true
            dragging = false
            startOffset = Panel.handleOffset
        }

        onUpdated: function (points) {
            if (points.length === 0)
                return
            const point = points[0]
            if (!dragging && Math.hypot(point.x - point.startX, point.y - point.startY)
                    > root.dragThreshold)
                dragging = true
            // Up the screen is a larger offset. The handle catches up with the
            // finger the moment a drag starts, so the thumb ends where it
            // pressed on the pill rather than 10px below it (AC 54).
            if (dragging)
                Panel.moveHandle(startOffset + Math.round(point.startY - point.y))
        }

        // A tap raises on release, not on press (§4.10 Q2): on press it cannot
        // yet be told apart from the start of a drag.
        onReleased: function (points) {
            held = false
            if (dragging)
                Panel.placeHandle()
            else
                Panel.requestShow()
            dragging = false
        }

        // The grab is gone, and no release is coming. Neither a tap nor a place:
        // the handle goes back to where the press found it.
        onCanceled: function (points) {
            held = false
            if (dragging)
                Panel.moveHandle(startOffset)
            dragging = false
        }
    }
}
