#pragma once

#include <QObject>
#include <QString>
#include <cstdint>

struct zwp_input_method_v2;
class WaylandConnection;

// The text half of Wayland input: apps that speak zwp_text_input_v3 (GTK4, Qt,
// most modern toolkits) never see key events at all. They negotiate with an
// input method, and this is that input method.
//
// Everything the compositor tells us is DOUBLE-BUFFERED. activate, deactivate,
// surrounding_text, text_change_cause and content_type all modify a pending
// state that only becomes current on `done`. Reading the pending values
// directly is the classic way to get a keyboard that shows itself half a beat
// early and picks the wrong layout.
class InputMethod : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(Purpose purpose READ purpose NOTIFY stateChanged)
    Q_PROPERTY(bool sensitive READ isSensitive NOTIFY stateChanged)

public:
    // Values of zwp_text_input_v3.content_purpose. Declared here rather than
    // generated: input-method-unstable-v2.xml only *references* text-input-v3's
    // enums for documentation, and wayland-scanner does not emit them, so
    // pulling in the whole text-input-v3 protocol to get fourteen integers
    // would be the wrong trade.
    enum Purpose {
        Normal = 0,
        Alpha = 1,
        Digits = 2,
        Number = 3,
        Phone = 4,
        Url = 5,
        Email = 6,
        Name = 7,
        Password = 8,
        Pin = 9,
        Date = 10,
        Time = 11,
        Datetime = 12,
        Terminal = 13,
    };
    Q_ENUM(Purpose)

    // Values of zwp_text_input_v3.content_hint, same reasoning.
    enum Hint {
        HintNone = 0x0,
        Completion = 0x1,
        Spellcheck = 0x2,
        AutoCapitalization = 0x4,
        Lowercase = 0x8,
        Uppercase = 0x10,
        Titlecase = 0x20,
        HiddenText = 0x40,
        SensitiveData = 0x80,
        Latin = 0x100,
        Multiline = 0x200,
    };
    Q_ENUM(Hint)

    explicit InputMethod(QObject *parent = nullptr);
    ~InputMethod() override;

    bool init(WaylandConnection *connection, QString *error);

    bool isActive() const { return m_current.active; }
    Purpose purpose() const { return m_current.purpose; }
    uint32_t hint() const { return m_current.hint; }

    // True for password and PIN fields, and for anything flagged as hidden or
    // sensitive. Suppresses long-press previews (AC 16).
    bool isSensitive() const;

    void commitString(const QString &text);

    // No deleteSurroundingText wrapper. It is the protocol's way to do
    // backspace on the text path, and this keyboard deliberately does not use
    // it -- see the note on handleSurroundingText in the .cpp. Backspace is a
    // keycode in both paths, which is correct in every case and needs no
    // bookkeeping.

Q_SIGNALS:
    void activeChanged();
    void stateChanged();

    // Every `activate`, including one that does not change the active state.
    //
    // activeChanged is not enough to raise the keyboard on its own, and relying
    // on it alone was a bug: when focus moves from one text field to another
    // the compositor coalesces deactivate and activate into a single `done`, so
    // the active state goes true -> true and no change is ever signalled. So
    // the rise is taken from here and only the fall from activeChanged.
    void activated();
    // Another input method already holds the seat. Fatal: two keyboards fighting
    // over one seat is worse than none (AC 7).
    void unavailable();

private:
    struct State {
        bool active = false;
        Purpose purpose = Normal;
        uint32_t hint = HintNone;
    };

    static void handleActivate(void *data, zwp_input_method_v2 *);
    static void handleDeactivate(void *data, zwp_input_method_v2 *);
    static void handleSurroundingText(void *data, zwp_input_method_v2 *,
                                      const char *text, uint32_t cursor, uint32_t anchor);
    static void handleTextChangeCause(void *data, zwp_input_method_v2 *, uint32_t cause);
    static void handleContentType(void *data, zwp_input_method_v2 *,
                                  uint32_t hint, uint32_t purpose);
    static void handleDone(void *data, zwp_input_method_v2 *);
    static void handleUnavailable(void *data, zwp_input_method_v2 *);

    WaylandConnection *m_connection = nullptr;
    zwp_input_method_v2 *m_inputMethod = nullptr;

    State m_pending;
    State m_current;

    // The one number that must be right. zwp_input_method_v2.commit takes "the
    // number of done events already issued by that object" -- not a Wayland
    // object serial, not a timestamp. Send the wrong value and the compositor
    // processes the request but discards the state change, so characters vanish
    // intermittently with nothing logged anywhere.
    uint32_t m_doneCount = 0;
};
