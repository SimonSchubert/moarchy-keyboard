import QtQuick
import QtQuick.Shapes
import moarchy

// The keyboard's icons, drawn rather than typed.
//
// Named DrawnIcon rather than KeyIcon because the C++ enum that selects one is
// already called KeyIcon, and two QML types of one name is an ambiguous import
// rather than an error -- qmllint catches it, the engine picks one.
//
// Why drawn at all is in the note above KeyIcon in src/keyspec.h: the obvious
// characters are simply not in Noto Sans, so each one used to arrive from a
// different substituted font -- the return arrow from Adwaita Mono, erase and
// shift from JetBrains Mono, the plain arrows from Noto Sans Symbols. Three
// type designs, three stroke weights, three ideas of where the ink sits. Shift
// and erase were also drawn as hollow outlines while the arrows were single
// strokes, so the set did not even agree with itself, and measured against the
// keycap centre they sat 5 physical pixels high.
//
// So: one grid, one stroke weight, one language. Every icon is a stroked
// polyline on a 24x24 box with round caps and joins -- round because the keys
// are drawn with a 5px corner radius and a mitred icon on a rounded key looks
// borrowed from somewhere else.
//
// QtQuick.Shapes comes from qt6-declarative, which is already a dependency, so
// AC 41 is untouched. The default (geometry) renderer is deliberate: these are
// straight polylines, so it has nothing to gain from the curve renderer, and
// that one wants derivatives this Mali-400 does not advertise.
Item {
    id: root

    required property int icon
    required property color strokeColor

    // The box the icon is drawn in. Bigger than the 13px the old glyphs were
    // sized at, because those were sized as if they were the word "esc".
    property real size: 20
    // In grid units, so it scales with the box.
    property real weight: 2.0

    implicitWidth: size
    implicitHeight: size
    visible: root.icon !== KeyIcon.NoIcon

    // Every path is a polyline on the same 24x24 grid, with the same 5-unit
    // arrowheads and the same 13-unit shafts, so the four arrows are the same
    // drawing rotated and the rest sit in the same optical weight.
    // Every path is a polyline on the same 24x24 grid, with the same 5-unit
    // arrowheads and the same 13-unit shafts, so the four arrows are one
    // drawing rotated and the rest carry the same optical weight.
    //
    // Each path's bounding box is centred on (12, 12) to within a tenth of a
    // unit, deliberately and by arithmetic rather than by eye: a shape centred
    // in the key is only actually centred if the drawing inside it is centred
    // too, and half a unit here is a visible pixel on the phone. Checked by
    // measuring the rendered ink, not by looking.
    readonly property string path: {
        switch (root.icon) {
        // Down the right, left along the bottom, into a head: the return arrow
        // every keyboard has drawn since the typewriter carriage.
        // Box 5.5..18.5 x 5..19.
        case KeyIcon.Enter:      return "M18.5 5 V14 H5.5 M11 9 L5.5 14 L11 19"
        // The pentagon with a cross in it, but stroked at the same weight as
        // everything else rather than as a hollow monospace box glyph. The
        // cross sits on the middle of the body, not of the whole outline, or it
        // reads as pushed towards the point. Box 3.5..20.5 x 5..19.
        case KeyIcon.Backspace:  return "M10 5.5 H19.5 V18.5 H10 L4.5 12 Z"
                                      + " M12.6 9.6 L16.9 14.4 M16.9 9.6 L12.6 14.4"
        // Box 5..19 both ways. Drawn to nearly the same extent as an arrow
        // rather than to the grid's edge: on the same 24-unit box a shape this
        // wide renders a third larger than the arrows beside it, and shift
        // sitting bigger than the arrow keys is the kind of thing you see
        // without being able to say why.
        case KeyIcon.Shift:      return "M12 5 L19 12 H15.5 V19 H8.5 V12 H5 Z"
        // Box 5.5..18.5 both ways, so the four are exactly one another turned.
        case KeyIcon.ArrowLeft:  return "M18.5 12 H5.5 M10.5 7 L5.5 12 L10.5 17"
        case KeyIcon.ArrowRight: return "M5.5 12 H18.5 M13.5 7 L18.5 12 L13.5 17"
        case KeyIcon.ArrowUp:    return "M12 18.5 V5.5 M7 10.5 L12 5.5 L17 10.5"
        case KeyIcon.ArrowDown:  return "M12 5.5 V18.5 M7 13.5 L12 18.5 L17 13.5"
        }
        return ""
    }

    // Drawn on the grid and scaled as a whole, so the stroke scales with it and
    // the icon is centred by construction -- which is the other half of why the
    // old glyphs were wrong. A shape centred in the key IS centred in the key;
    // a glyph is centred on its own font's idea of a line box.
    Item {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: root.size / 24
        antialiasing: true

        Shape {
            anchors.fill: parent
            antialiasing: true
            ShapePath {
                strokeColor: root.strokeColor
                strokeWidth: root.weight
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.path }
            }
        }
    }
}
