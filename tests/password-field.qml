// A password field, for AC 16.
//
// Plain QtQuick TextInput rather than QtQuick.Controls, so this needs only
// qt6-declarative -- already on the phone for the shell. `echoMode: Password`
// is what makes Qt advertise zwp_text_input_v3 content_purpose = password (8),
// which is the thing under test: the keyboard must suppress long-press previews
// and alternate hints when it sees it.
//
//   qml6 tests/password-field.qml
import QtQuick

// Sets an app_id of moa-pwtest so a stray window is attributable. The Qt
// default is org.qt-project.qml, which looks like nobody's and turned up in
// another session's test output as an unexplained window.
Window {
    width: 360; height: 200
    visible: true
    title: "moa password test"
    // Qt derives the Wayland app_id from the desktop file name.
    Component.onCompleted: Qt.application.name = "moa-pwtest"
    color: "#1e1e2e"

    Column {
        anchors.centerIn: parent
        spacing: 14

        Text { text: "password (purpose 8):"; color: "#cdd6f4" }

        Rectangle {
            width: 300; height: 40; color: "#313244"; radius: 4
            TextInput {
                id: secret
                anchors.fill: parent
                anchors.margins: 8
                echoMode: TextInput.Password
                color: "#cdd6f4"
                font.pixelSize: 18
                focus: true
            }
        }

        Text {
            text: "normal (purpose 0):"; color: "#cdd6f4"
        }

        Rectangle {
            width: 300; height: 40; color: "#313244"; radius: 4
            TextInput {
                anchors.fill: parent
                anchors.margins: 8
                color: "#cdd6f4"
                font.pixelSize: 18
            }
        }
    }
}
