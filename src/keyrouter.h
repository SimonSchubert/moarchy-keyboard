#pragma once

#include "qmlsingleton.h"

#include <QQmlEngine>
#include <QtGlobal>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include "virtualkeyboard.h"

class InputMethod;

// Decides, per key press, whether the character goes out as text or as a
// keycode. This is the whole reason both protocols are bound; see SPEC.md §5.
//
//   focused client        printable key                 named / chorded key
//   --------------------  ----------------------------  --------------------
//   has text-input-v3     commit_string + commit(serial) virtual-keyboard
//   has not               virtual-keyboard               virtual-keyboard
//
// A keyboard that only commits text cannot type into a terminal. One that only
// sends keycodes cannot type a character that is not on the keymap. Both, or
// half the phone is unusable.
class KeyRouter : public QObject, public MainOwnedSingleton<KeyRouter>
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Router)
    QML_SINGLETON
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(bool sensitive READ isSensitive NOTIFY stateChanged)
    Q_PROPERTY(QString suggestedLayout READ suggestedLayout NOTIFY stateChanged)

public:
    // Not default-constructible, which is the only reason this singleton
    // worked while Theme and LayoutStore silently did not: a QML_SINGLETON that
    // CAN be default-constructed is, and create() is never called. It went
    // years without the static_assert that says so; it has one now.
    KeyRouter(InputMethod *inputMethod, VirtualKeyboard *virtualKeyboard,
              QObject *parent = nullptr);

    bool isActive() const;
    bool isSensitive() const;

    // "numeric" for digit and PIN fields, "terminal" for terminal purpose,
    // empty when the app expresses no preference. Only ever a suggestion: a
    // manual layout choice outranks it until focus changes (AC 21).
    QString suggestedLayout() const;

    // Called the instant a touch lands on a key, before any decision about
    // what that key does. Everything measured afterwards is measured from
    // here, so the two latency criteria are answered against the same origin:
    //
    //   AC 33 -- press to the frame that shows the key lit, <= one frame
    //   AC 38 -- press to commit_string reaching the compositor, <= 50 ms
    //
    // These are different numbers and get conflated easily. Feedback happens
    // on press and emission happens on release, so a slow commit does not make
    // the keyboard feel slow, and a fast commit does not make it feel fast.
    Q_INVOKABLE void markPress();

    // Emission happens on release, not on press (so that sliding off a key can
    // cancel it -- AC 31). So press-to-wire contains the whole time the finger
    // was down, which is the user's, not the keyboard's: the first measurement
    // read 90 ms and 90 ms was exactly the synthetic hold the test used.
    // AC 38 is about latency the keyboard adds, so it is measured from release.
    Q_INVOKABLE void markRelease();

    // Connected to QQuickWindow::frameSwapped, to close the AC 33 loop.
    void noteFrame();

    // A printable character (or a short string -- an emoji is several code
    // units). Takes the text path when one is available.
    Q_INVOKABLE void sendText(const QString &text);

    // A named key: "BackSpace", "Return", "Tab", "Escape", "Left", ... Always
    // the keycode path, in both directions, including Backspace.
    //
    // Backspace could go out as delete_surrounding_text on the text path, and
    // deliberately does not: that request counts BYTES of surrounding text, and
    // our copy of the surrounding text is stale the moment a commit_string has
    // gone out with no intervening `done`. A keycode is correct in every case
    // and needs no bookkeeping (AC 14).
    Q_INVOKABLE void sendKey(const QString &name, int modifiers = 0);

    // A printable character forced through the keycode path because it is
    // chorded with a modifier -- Ctrl+C is a key event, not the letter c.
    Q_INVOKABLE void sendChord(const QString &character, int modifiers);

Q_SIGNALS:
    void activeChanged();
    void stateChanged();

private:
    // Deliberately holds NO modifier state of its own. The keyboard's shift /
    // ctrl / alt latching lives in Keyboard.qml, which passes the modifiers it
    // wants with each key. A second copy here was dead -- nothing set it -- and
    // worse than dead: the setter pushed them onto the SEAT as held modifiers,
    // where they would have applied to a plugged-in USB keyboard too, and
    // stayed applied until something cleared them.
    void reportLatency(const char *what, const QString &detail);

    QElapsedTimer m_sincePress;
    QElapsedTimer m_sinceRelease;
    bool m_frameReported = true;

    InputMethod *m_inputMethod = nullptr;
    VirtualKeyboard *m_virtualKeyboard = nullptr;

};

// See MainOwnedSingleton in qmlsingleton.h: a default-constructible
// QML_SINGLETON is built by the engine instead of by create(), and QML then
// holds a different object from the one main() configured. That shipped once
// and drew every surface black, so it is a build error instead.
MOARCHY_SINGLETON_INVARIANT(KeyRouter);
