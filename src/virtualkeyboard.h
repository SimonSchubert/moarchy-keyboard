#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <cstdint>

struct zwp_virtual_keyboard_v1;
class WaylandConnection;

// Synthesises real key events, for every client that does not speak
// text-input-v3 -- which on this phone means the terminal, and every Xwayland
// app.
//
// The keymap is uploaded exactly once, at startup, over a memfd. Every keycode
// sent afterwards is an index into *that* keymap, not into whatever layout the
// compositor has configured for the physical keyboard. Sway treats a virtual
// keyboard as its own input device with its own layout, so this does not
// disturb the USB keyboard if one is plugged in.
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

    bool init(WaylandConnection *connection, QString *error);

    // Press and release one key with the given modifiers held around it.
    void tap(uint32_t linuxKeycode, Modifiers modifiers);

    // Held modifier state, for latched Shift/Ctrl/Alt (AC 34).
    void setHeldModifiers(Modifiers modifiers);

private:
    uint32_t xkbMask(Modifiers modifiers) const;
    void sendModifiers(Modifiers modifiers);
    uint32_t timestamp();

    WaylandConnection *m_connection = nullptr;
    zwp_virtual_keyboard_v1 *m_keyboard = nullptr;
    QElapsedTimer m_clock;

    // Masks looked up from the compiled keymap rather than hardcoded: the
    // numeric values of Mod1/Mod4/Mod5 are a property of the keymap, not of the
    // protocol.
    uint32_t m_shiftMask = 0;
    uint32_t m_controlMask = 0;
    uint32_t m_altMask = 0;
    uint32_t m_superMask = 0;
    uint32_t m_altGrMask = 0;

    Modifiers m_held = NoModifiers;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(VirtualKeyboard::Modifiers)
