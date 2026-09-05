#pragma once

#include <QQmlEngine>
#include <QtGlobal>

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
class KeyRouter : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Router)
    QML_SINGLETON
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(bool sensitive READ isSensitive NOTIFY stateChanged)
    Q_PROPERTY(QString suggestedLayout READ suggestedLayout NOTIFY stateChanged)
    Q_PROPERTY(int latchedModifiers READ latchedModifiers WRITE setLatchedModifiers
                   NOTIFY latchedModifiersChanged)

public:
    // See the note above the class.
    static void setInstance(KeyRouter *instance) { s_instance = instance; }
    static KeyRouter *create(QQmlEngine *, QJSEngine *) {
        // NOT Q_ASSERT: it compiles out under NDEBUG, and the packaged build is
        // Release, so a null instance would be a segfault in the field and a
        // clean abort only on a developer's machine. Returning nullptr makes
        // QML report an unavailable singleton, which is loud and survivable.
        if (!s_instance) {
            qCritical("KeyRouter singleton used before main() set the instance");
            return nullptr;
        }
        // The engine must never delete an object main owns.
        QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
        return s_instance;
    }

    KeyRouter(InputMethod *inputMethod, VirtualKeyboard *virtualKeyboard,
              QObject *parent = nullptr);

    bool isActive() const;
    bool isSensitive() const;

    // "numeric" for digit and PIN fields, "terminal" for terminal purpose,
    // empty when the app expresses no preference. Only ever a suggestion: a
    // manual layout choice outranks it until focus changes (AC 21).
    QString suggestedLayout() const;

    int latchedModifiers() const { return int(m_latched); }
    void setLatchedModifiers(int modifiers);

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
    void latchedModifiersChanged();

private:
    InputMethod *m_inputMethod = nullptr;
    VirtualKeyboard *m_virtualKeyboard = nullptr;
    VirtualKeyboard::Modifiers m_latched = VirtualKeyboard::NoModifiers;

    inline static KeyRouter *s_instance = nullptr;
};
