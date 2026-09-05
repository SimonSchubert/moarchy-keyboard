pragma ComponentBehavior: Bound

import QtQuick
import moarchy

// The tab that brings a dismissed keyboard back.
//
// It exists because of a gap in the platform, not because a keyboard wants
// decoration. Dismissing the keyboard by gesture leaves the text field focused,
// and tapping that field again emits nothing at all -- measured with
// WAYLAND_DEBUG: zero input-method traffic, because nothing about the client's
// text state changed. So there is no event to wake up on, and without something
// to tap the keyboard is unreachable until focus moves elsewhere.
//
// Which means this is only ever shown in exactly that state: dismissed by hand,
// with a text input still active. Any other time it would be clutter, and the
// panel draws nothing at all.
Item {
    id: root

    readonly property int pillWidth: 54
    readonly property int pillHeight: 30

    implicitWidth: pillWidth
    implicitHeight: pillHeight

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: height / 2

        // Reads as a raised control rather than as a key: the key fill would
        // make it look like a stray key floating over the app.
        color: Colors.modifierFill
        border.width: 1
        border.color: Colors.blend(Colors.modifierText, Colors.modifierFill, 0.22)

        // Pressed state, because a control with no feedback feels broken on a
        // touchscreen even when it works.
        scale: touch.pressed ? 0.94 : 1.0
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

    MultiPointTouchArea {
        id: touch
        anchors.fill: parent
        mouseEnabled: true
        onPressed: Panel.requestShow()
    }
}
