#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <cstdint>

struct zwp_virtual_keyboard_v1;
class WaylandConnection;

// Synthesises real key events, for every client that does not speak
// text-input-v3 -- which on this phone means the terminal, and every Xwayland
// app.
//
// ---------------------------------------------------------------------------
// The keymap is generated, not borrowed
// ---------------------------------------------------------------------------
// A plain "us" keymap can express ASCII and nothing else, so every character
// the layouts offer beyond it -- the accented letters behind a long-press, the
// euro sign, an em dash -- could only ever be typed through the text path. In a
// terminal, which has no text input, they would silently vanish.
//
// So the keymap is compiled at startup from the union of every character the
// loaded layouts can produce: the standard us layout for the ASCII half (so
// Escape, Tab, the arrows and Ctrl chords keep the keycodes everything expects)
// plus one spare keycode per extra character. wvkbd solves the same problem the
// same way.
//
// Uploaded exactly once, over a memfd. Every keycode sent afterwards indexes
// into *that* keymap, not into whatever layout the compositor has configured
// for the physical keyboard -- Sway treats a virtual keyboard as its own input
// device, so a plugged-in USB keyboard is unaffected.
class VirtualKeyboard : public QObject
{
    Q_OBJECT

public:
    enum Modifier {
        NoModifiers = 0,
        Shift = 1 << 0,
        Control = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3,
        AltGr = 1 << 4,
    };
    Q_DECLARE_FLAGS(Modifiers, Modifier)
    Q_FLAG(Modifiers)

    explicit VirtualKeyboard(QObject *parent = nullptr);
    ~VirtualKeyboard() override;

    // `characters` is every single-character string the layouts can emit.
    // Anything already reachable on us is ignored; the rest gets a spare
    // keycode. Passing an empty list yields a plain us keymap.
    bool init(WaylandConnection *connection, const QStringList &characters, QString *error);

    // Evdev keycode for a character, or 0 if it is on neither the us layout nor
    // the generated tail. *needsShift is set for characters that need it.
    uint32_t keycodeForCharacter(const QString &character, bool *needsShift) const;

    // Evdev keycode for a named key ("Escape", "BackSpace", "Left", ...), or 0.
    static uint32_t keycodeForName(const QString &name);

    // Press and release one key with `modifiers` held around it, and only
    // around it. There is deliberately no persistent held-modifier state: this
    // keyboard's latching lives in the QML, and pushing modifiers onto the seat
    // would apply them to a physical keyboard as well.
    void tap(uint32_t linuxKeycode, Modifiers modifiers);

    // How many characters needed a generated slot. Logged at startup; also the
    // thing to look at when a character mysteriously will not type.
    int generatedKeyCount() const { return m_generated.size(); }

    // Builds the keymap and compiles it, with no compositor involved. Exposed
    // so `--dump-keymap` can check it anywhere -- including in the build
    // container, which has xkbcommon but no Wayland. A keymap that fails to
    // compile costs every non-ASCII character in every layout, silently, so it
    // is worth being able to check without a phone.
    QString keymapSource(const QStringList &characters, QString *compileError);

private:
    QString buildKeymap(const QStringList &characters);
    uint32_t xkbMask(Modifiers modifiers) const;
    void sendModifiers(Modifiers modifiers);
    uint32_t timestamp();

    WaylandConnection *m_connection = nullptr;
    zwp_virtual_keyboard_v1 *m_keyboard = nullptr;
    QElapsedTimer m_clock;

    uint32_t m_shiftMask = 0;
    uint32_t m_controlMask = 0;
    uint32_t m_altMask = 0;
    uint32_t m_superMask = 0;
    uint32_t m_altGrMask = 0;

    // character -> evdev keycode, for the generated tail only.
    QHash<QChar, uint32_t> m_generated;

};

Q_DECLARE_OPERATORS_FOR_FLAGS(VirtualKeyboard::Modifiers)
