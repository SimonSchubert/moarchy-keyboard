// A full-screen probe that reports what input reaches it, through its TITLE.
//
// Answers two questions that need a client with NO text field:
//
//   AC 4b -- does a retracted keyboard pass touches through to the app under
//            it, and does a raised one stop them?
//   AC 11 -- does the keycode path work for a client that never binds
//            text-input-v3? foot cannot answer this: it speaks text-input-v3
//            even when what is running inside it is htop, so a key sent to it
//            arrives as a commit_string and the keycode path is never taken.
//
// Reporting goes in the window title because sway exposes titles in get_tree.
// Two things that do NOT work: writing a file with XMLHttpRequest PUT to a
// file:// URL (silently writes nothing), and setting Qt.application.name to
// control the Wayland app_id (too late -- app_id comes from the desktop file
// name at startup, so this window is always org.qt-project.qml and must be
// matched by title).
import QtQuick

Window {
    id: win
    visible: true
    // NOT fullscreen. sway renders a fullscreen window above the Top layer, so
    // a fullscreen probe hides the very keyboard it is meant to be testing
    // against. mobileomarchy tiles one app per workspace, so an ordinary window
    // fills the screen anyway -- minus the keyboard's exclusive zone, which is
    // exactly the geometry under test.
    width: 360
    height: 480
    color: taps > 0 ? "#a6e3a1" : "#1e1e2e"

    property int taps: 0
    property string lastKey: "none"

    title: "moa probe taps=" + taps + " key=" + lastKey

    // No TextInput anywhere in this file, deliberately: a focused text field
    // would make Qt bind text-input-v3 and activate the input method, which is
    // the exact condition AC 11 needs absent.
    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: function (event) {
            win.lastKey = event.text.length > 0 ? event.text : ("code" + event.key)
            // q quits, so a test can assert "the keycode arrived" by the window
            // being gone rather than by parsing anything.
            if (event.text === "q")
                Qt.quit()
        }
    }

    Text {
        anchors.centerIn: parent
        text: win.taps + "\n" + win.lastKey
        horizontalAlignment: Text.AlignHCenter
        color: "#1e1e2e"
        font.pixelSize: 90
    }

    MultiPointTouchArea {
        anchors.fill: parent
        mouseEnabled: true
        onPressed: win.taps++
    }
}
