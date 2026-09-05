pragma ComponentBehavior: Bound

import QtQuick
import moarchy

// Layout, state and touch.
//
// ---------------------------------------------------------------------------
// Why one MultiPointTouchArea and not a MouseArea per key
// ---------------------------------------------------------------------------
// MouseArea is single-touch. On a phone keyboard that means the second finger
// is ignored until the first lifts, which anyone typing at speed notices
// immediately as dropped letters (AC 30). One touch area over the whole panel,
// hit-testing each point against the row geometry, handles as many fingers as
// the panel has keys and costs one function call per point per event.
//
// ---------------------------------------------------------------------------
// Why keys fire on release, not on press
// ---------------------------------------------------------------------------
// So that sliding off a key can cancel it (AC 31). Visual feedback still
// happens on press, within the frame -- the two are separate on purpose, and
// AC 33 is about the feedback, not the emission. Auto-repeating keys
// (backspace) are the exception: they fire on press and repeat, because a
// backspace you have to lift for is useless.
Item {
    id: keyboard

    // -- layout ------------------------------------------------------------
    property string layoutName: Layouts.initialLayout
    property var layout: Layouts.layout(layoutName)
    // A layout chosen by hand outranks the app's suggestion, until focus moves
    // somewhere else and the whole question is reopened (AC 21).
    property bool layoutChosenByHand: false

    readonly property var rows: layout && layout.rows ? layout.rows : []
    readonly property real rowHeight: rows.length > 0 ? height / rows.length : height

    // How many key-widths the widest row spans -- NOT a hardcoded 10.
    //
    // The letters, symbols and terminal layouts are all 10 wide, so a constant
    // looked right and was: until the numeric layout, which is 4 wide. At
    // width/10 its keys came out 36px in a 360px panel, huddled in the middle
    // of the screen with 108px of dead space either side. A layout may declare
    // `columns` explicitly; otherwise it is measured.
    readonly property real columns: {
        if (layout && layout.columns !== undefined && layout.columns > 0)
            return layout.columns
        var widest = 1
        for (var i = 0; i < rows.length; ++i) {
            var keys = rows[i].keys
            var total = 0
            for (var j = 0; j < keys.length; ++j)
                total += keys[j].width !== undefined ? keys[j].width : 1
            if (total > widest)
                widest = total
        }
        return widest
    }

    readonly property real unit: width / columns

    // -- modifier state ----------------------------------------------------
    // 0 off, 1 latched for one key, 2 locked until pressed again (AC 34).
    property int shiftState: 0
    property int ctrlState: 0
    property int altState: 0

    readonly property bool shifted: shiftState > 0

    readonly property var modifierStates: ({
        shift: shiftState,
        ctrl: ctrlState,
        alt: altState
    })

    // Bit values from VirtualKeyboard::Modifier.
    readonly property int modShift: 1
    readonly property int modControl: 2
    readonly property int modAlt: 4

    readonly property int chordModifiers:
        (ctrlState > 0 ? modControl : 0) | (altState > 0 ? modAlt : 0)

    function selectLayout(name, byHand) {
        var next = Layouts.layout(name)
        if (!next || !next.rows) {
            console.warn("moarchy: no usable layout named", name, "- staying on", layoutName)
            return
        }
        layoutName = name
        layout = next
        layoutChosenByHand = byHand === true
        // A layout switch is a fresh start for one-shot modifiers, but a locked
        // shift is a deliberate state and survives.
        if (shiftState === 1)
            shiftState = 0
    }

    function applySuggestion() {
        if (layoutChosenByHand)
            return

        var suggested = Router.suggestedLayout

        // An EMPTY suggestion is a real answer -- "this app wants the ordinary
        // keyboard" -- not an absence of one. Treating it as "leave things
        // alone" meant the terminal layout, once auto-selected for a terminal,
        // stayed selected for every app focused afterwards. Type in a terminal,
        // then tap a search box, and you got esc/tab/ctrl/alt and arrow keys
        // with no letters row until you switched by hand.
        if (suggested === "")
            suggested = Layouts.initialLayout

        if (suggested !== layoutName)
            selectLayout(suggested, false)
    }

    Connections {
        target: Router
        function onStateChanged() { keyboard.applySuggestion() }
        function onActiveChanged() {
            // New focus, so a hand-picked layout stops being sticky and the
            // app's own preference gets to apply again.
            keyboard.layoutChosenByHand = false
            keyboard.ctrlState = 0
            keyboard.altState = 0
            if (keyboard.shiftState === 1)
                keyboard.shiftState = 0
            keyboard.applySuggestion()
        }
    }

    // -- geometry ----------------------------------------------------------
    Column {
        id: rowsColumn
        anchors.fill: parent

        Repeater {
            id: rowRepeater
            model: keyboard.rows
            delegate: KeyRow {
                required property var modelData
                keys: modelData.keys
                unit: keyboard.unit
                shifted: keyboard.shifted
                modifierStates: keyboard.modifierStates
                width: rowsColumn.width
                height: keyboard.rowHeight
            }
        }
    }

    AlternatesPopup { id: popup }

    // -- hit testing -------------------------------------------------------
    function keyAt(x, y) {
        if (rows.length === 0)
            return null
        var index = Math.floor(y / rowHeight)
        index = Math.max(0, Math.min(rows.length - 1, index))
        // `as KeyRow` is not decoration: itemAt() is typed QQuickItem, so
        // without the cast qmllint cannot see keyAt at all and a rename would
        // fail silently at runtime instead of loudly at build time.
        var row = rowRepeater.itemAt(index) as KeyRow
        return row ? row.keyAt(x, y - index * rowHeight) : null
    }

    // -- activation --------------------------------------------------------
    function consumeOneShots() {
        if (shiftState === 1)
            shiftState = 0
        if (ctrlState === 1)
            ctrlState = 0
        if (altState === 1)
            altState = 0
    }

    function cycleModifier(state) {
        // off -> latched -> locked -> off. Two taps to lock is the convention
        // every phone keyboard uses and the only one that needs no explaining.
        return (state + 1) % 3
    }

    function activate(cap) {
        if (!cap)
            return
        var spec = cap.spec

        switch (cap.keyType) {
        case "modifier":
            if (spec.modifier === "shift")
                shiftState = cycleModifier(shiftState)
            else if (spec.modifier === "ctrl")
                ctrlState = cycleModifier(ctrlState)
            else if (spec.modifier === "alt")
                altState = cycleModifier(altState)
            return

        case "layout":
            selectLayout(spec.layout, true)
            return

        case "action":
            Router.sendKey(spec.key, chordModifiers)
            consumeOneShots()
            return
        }

        // Character or space.
        var text = spec.text !== undefined ? spec.text : ""
        if (text === "")
            return

        if (chordModifiers !== 0) {
            // Ctrl+C is a key event, not the letter c: it has to leave as a
            // keycode even when the focused app speaks text-input-v3, or the
            // terminal gets a literal "c" and the interrupt never happens.
            Router.sendChord(text, chordModifiers)
            consumeOneShots()
            return
        }

        Router.sendText(shifted ? text.toUpperCase() : text)
        consumeOneShots()
    }

    function commitAlternate() {
        var value = popup.selected()
        popup.alternates = []
        if (value !== "")
            Router.sendText(value)
        consumeOneShots()
    }

    // -- touch -------------------------------------------------------------
    // pointId -> { cap, cancelled, longPress }
    property var tracked: ({})
    property int longPressPointId: -1

    Timer {
        id: longPressTimer
        interval: 400
        onTriggered: {
            var entry = keyboard.tracked[keyboard.longPressPointId]
            if (!entry || entry.cancelled || !entry.cap)
                return
            if (!entry.cap.hasAlternates || Router.sensitive)
                return

            entry.longPress = true
            popup.alternates = entry.cap.alternates
            popup.selectedIndex = 0

            var cap = entry.cap
            var origin = cap.mapToItem(keyboard, 0, 0)
            popup.x = Math.max(0, Math.min(keyboard.width - popup.width,
                                           origin.x + cap.width / 2 - popup.cellWidth / 2))
            popup.y = Math.max(0, origin.y - popup.height - 4)
        }
    }

    Timer {
        id: repeatTimer
        interval: 55
        repeat: true
        onTriggered: {
            var entry = keyboard.tracked[keyboard.longPressPointId]
            if (!entry || entry.cancelled || !entry.cap || !entry.cap.repeats) {
                stop()
                return
            }
            keyboard.activate(entry.cap)
        }
    }

    Timer {
        id: repeatDelay
        interval: 400
        onTriggered: repeatTimer.start()
    }

    MultiPointTouchArea {
        anchors.fill: parent
        maximumTouchPoints: 5
        mouseEnabled: true

        onPressed: function (points) {
            for (var i = 0; i < points.length; ++i) {
                var point = points[i]
                var cap = keyboard.keyAt(point.x, point.y)
                if (!cap)
                    continue

                // Before anything else, so the latency numbers start at the
                // touch rather than at whatever this handler decides to do.
                Router.markPress()

                cap.pressed = true
                keyboard.tracked[point.pointId] = { cap: cap, cancelled: false, longPress: false }

                if (cap.repeats) {
                    // Fires immediately, then repeats. Everything else waits for
                    // the release.
                    keyboard.activate(cap)
                    keyboard.longPressPointId = point.pointId
                    repeatDelay.restart()
                } else if (cap.hasAlternates) {
                    keyboard.longPressPointId = point.pointId
                    longPressTimer.restart()
                }
            }
        }

        onUpdated: function (points) {
            for (var i = 0; i < points.length; ++i) {
                var point = points[i]
                var entry = keyboard.tracked[point.pointId]
                if (!entry || !entry.cap)
                    continue

                if (entry.longPress) {
                    popup.selectedIndex = popup.indexAt(point.x)
                    continue
                }

                // Slide off and the press is abandoned (AC 31). The key stays
                // tracked so the release does not then fire whatever is under
                // the finger by then.
                var under = keyboard.keyAt(point.x, point.y)
                if (under !== entry.cap && !entry.cancelled) {
                    entry.cancelled = true
                    entry.cap.pressed = false
                    if (keyboard.longPressPointId === point.pointId) {
                        longPressTimer.stop()
                        repeatDelay.stop()
                        repeatTimer.stop()
                    }
                }
            }
        }

        onReleased: function (points) {
            for (var i = 0; i < points.length; ++i) {
                var point = points[i]
                var entry = keyboard.tracked[point.pointId]
                if (!entry)
                    continue

                if (entry.cap)
                    entry.cap.pressed = false

                if (keyboard.longPressPointId === point.pointId) {
                    longPressTimer.stop()
                    repeatDelay.stop()
                    repeatTimer.stop()
                    keyboard.longPressPointId = -1
                }

                if (entry.longPress)
                    keyboard.commitAlternate()
                else if (!entry.cancelled && entry.cap && !entry.cap.repeats)
                    keyboard.activate(entry.cap)

                delete keyboard.tracked[point.pointId]
            }
        }

        onCanceled: function (points) {
            for (var i = 0; i < points.length; ++i) {
                var entry = keyboard.tracked[points[i].pointId]
                if (entry && entry.cap)
                    entry.cap.pressed = false
                delete keyboard.tracked[points[i].pointId]
            }
            popup.alternates = []
            longPressTimer.stop()
            repeatDelay.stop()
            repeatTimer.stop()
        }
    }

    Component.onCompleted: {
        if (!layout || !layout.rows)
            console.warn("moarchy: layout", layoutName, "did not load; keyboard will be blank")
        applySuggestion()
    }
}
