// A plain text field whose contents appear in the window title.
//
// Needed because the obvious target -- a terminal -- is the wrong one for the
// touch criteria. foot advertises content_purpose = terminal, so the keyboard
// correctly switches to its terminal layout, and coordinates worked out for the
// letters layout then land on esc and tab. Three criteria were measured against
// the wrong keys that way.
//
// This is `purpose 0`, an ordinary text field, so the letters layout stays put
// and the long-press alternates exist. The title carries the text, so a test
// reads a string instead of interpreting a screenshot of a terminal.
import QtQuick

Window {
    id: win
    visible: true
    width: 360
    height: 260
    color: "#1e1e2e"

    // focus= tells a test whether a synthetic tap actually landed on the
    // field, which is the difference between "the platform sent no event" and
    // "my tap missed".
    title: "moa text [" + field.text + "] focus=" + field.activeFocus

    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            text: "ordinary field (purpose 0)"
            color: "#cdd6f4"
            font.pixelSize: 14
        }

        Rectangle {
            width: 300
            height: 56
            radius: 4
            color: "#313244"

            TextInput {
                id: field
                anchors.fill: parent
                anchors.margins: 10
                color: "#cdd6f4"
                font.pixelSize: 22
                focus: true
                verticalAlignment: TextInput.AlignVCenter
            }
        }
    }
}
