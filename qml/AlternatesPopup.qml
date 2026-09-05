import QtQuick

// The long-press alternates strip, drawn inside the panel.
//
// Not a zwp_input_popup_surface_v2, which is what the protocol offers for
// exactly this: a popup surface has to be built from a wl_surface on the same
// connection as the input method, and the input method deliberately lives on a
// second connection of its own (see src/waylandconnection.h). Drawing inside the
// panel keeps that separation intact, and on a 300px panel there is room above
// the top row anyway.
Item {
    id: root

    property var alternates: []
    property int selectedIndex: 0
    readonly property real cellWidth: 40

    visible: alternates.length > 0
    width: Math.max(1, alternates.length) * cellWidth
    height: 46

    function indexAt(globalX) {
        var local = globalX - x
        return Math.max(0, Math.min(alternates.length - 1, Math.floor(local / cellWidth)))
    }

    function selected() {
        if (alternates.length === 0)
            return ""
        return alternates[Math.max(0, Math.min(alternates.length - 1, selectedIndex))]
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Colors.modifierFill
        border.width: 1
        border.color: Colors.accent

        Row {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 0

            Repeater {
                model: root.alternates
                delegate: Rectangle {
                    required property var modelData
                    required property int index

                    width: root.cellWidth
                    height: parent.height
                    radius: 4
                    color: index === root.selectedIndex ? Colors.accent : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: parent.index === root.selectedIndex
                               ? Colors.accentText : Colors.modifierText
                        font.pixelSize: 19
                        renderType: Text.NativeRendering
                    }
                }
            }
        }
    }
}
