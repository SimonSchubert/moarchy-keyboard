// A full-screen surface that counts touches and writes the count to a file.
//
// Exists because "did that tap reach the app underneath?" cannot be answered by
// screenshotting a terminal: htop redraws on its own, so a before/after
// comparison always differs and always passes. And it cannot be answered by
// tapping a terminal either, because foot does not turn touch into a click.
//
// This counts wl_touch events and writes the total to /tmp/moa-touch-count, so
// the test reads a number instead of interpreting a picture.
//
//   qml6 tests/touch-probe.qml
import QtQuick

Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    title: "moa touch probe"
    color: taps > 0 ? "#a6e3a1" : "#1e1e2e"

    property int taps: 0

    Component.onCompleted: Qt.application.name = "moa-touch"

    Text {
        anchors.centerIn: parent
        text: win.taps
        color: "#1e1e2e"
        font.pixelSize: 120
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        text: "taps land here"
        color: "#cdd6f4"
        font.pixelSize: 16
    }

    MultiPointTouchArea {
        anchors.fill: parent
        mouseEnabled: true
        onPressed: {
            win.taps++
            // XMLHttpRequest with a file: URL is the only way a bare `qml`
            // runtime can write a file without a C++ helper. Ugly, and it means
            // the test reads a number rather than squinting at a screenshot.
            var request = new XMLHttpRequest()
            request.open("PUT", "file:///tmp/moa-touch-count")
            request.send(String(win.taps))
        }
    }
}
