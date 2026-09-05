#include "keyrouter.h"

#include "inputmethod.h"

#include <QHash>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcRouter, "moarchy.router")

namespace {

// Linux evdev keycodes. zwp_virtual_keyboard_v1.key carries the same values as
// wl_keyboard.key -- evdev codes, i.e. the xkb keycode minus 8.
struct CharacterKey {
    uint32_t keycode;
    bool shifted;
};

// Built once from the us layout rows, in evdev order, so the tables cannot
// drift out of step with each other the way two hand-written lists would.
const QHash<QChar, CharacterKey> &characterTable()
{
    static const QHash<QChar, CharacterKey> table = [] {
        QHash<QChar, CharacterKey> map;

        struct Row {
            const char *unshifted;
            const char *shifted;
            uint32_t first;
        };
        static const Row rows[] = {
            { "1234567890-=",  "!@#$%^&*()_+",  2  },
            { "qwertyuiop[]",  "QWERTYUIOP{}",  16 },
            { "asdfghjkl;'`",  "ASDFGHJKL:\"~", 30 },
            { "\\zxcvbnm,./",  "|ZXCVBNM<>?",   43 },
        };

        for (const Row &row : rows) {
            const QString plain = QString::fromLatin1(row.unshifted);
            const QString shift = QString::fromLatin1(row.shifted);
            Q_ASSERT(plain.size() == shift.size());
            for (int i = 0; i < plain.size(); ++i) {
                map.insert(plain.at(i), { row.first + uint32_t(i), false });
                map.insert(shift.at(i), { row.first + uint32_t(i), true });
            }
        }
        map.insert(QLatin1Char(' '), { 57, false });
        return map;
    }();
    return table;
}

const QHash<QString, uint32_t> &namedKeyTable()
{
    static const QHash<QString, uint32_t> table = {
        { QStringLiteral("Escape"),    1   },
        { QStringLiteral("BackSpace"), 14  },
        { QStringLiteral("Tab"),       15  },
        { QStringLiteral("Return"),    28  },
        { QStringLiteral("space"),     57  },
        { QStringLiteral("Home"),      102 },
        { QStringLiteral("Up"),        103 },
        { QStringLiteral("PageUp"),    104 },
        { QStringLiteral("Left"),      105 },
        { QStringLiteral("Right"),     106 },
        { QStringLiteral("End"),       107 },
        { QStringLiteral("Down"),      108 },
        { QStringLiteral("PageDown"),  109 },
        { QStringLiteral("Insert"),    110 },
        { QStringLiteral("Delete"),    111 },
    };
    return table;
}

} // namespace

KeyRouter::KeyRouter(InputMethod *inputMethod, VirtualKeyboard *virtualKeyboard, QObject *parent)
    : QObject(parent)
    , m_inputMethod(inputMethod)
    , m_virtualKeyboard(virtualKeyboard)
{
    connect(m_inputMethod, &InputMethod::activeChanged, this, &KeyRouter::activeChanged);
    connect(m_inputMethod, &InputMethod::stateChanged, this, &KeyRouter::stateChanged);
}

bool KeyRouter::isActive() const
{
    return m_inputMethod->isActive();
}

bool KeyRouter::isSensitive() const
{
    return m_inputMethod->isSensitive();
}

QString KeyRouter::suggestedLayout() const
{
    switch (m_inputMethod->purpose()) {
    case InputMethod::Digits:
    case InputMethod::Number:
    case InputMethod::Phone:
    case InputMethod::Pin:
        return QStringLiteral("numeric");
    case InputMethod::Terminal:
        return QStringLiteral("terminal");
    default:
        return QString();
    }
}

void KeyRouter::setLatchedModifiers(int modifiers)
{
    const auto value = VirtualKeyboard::Modifiers(modifiers);
    if (value == m_latched)
        return;
    m_latched = value;

    // Shift is applied per-key on the text path and does not belong in the
    // seat's held state, or every subsequent keycode would be shifted too.
    m_virtualKeyboard->setHeldModifiers(value & ~VirtualKeyboard::Modifiers(VirtualKeyboard::Shift));
    Q_EMIT latchedModifiersChanged();
}

uint32_t KeyRouter::keycodeForCharacter(const QString &character, bool *needsShift)
{
    *needsShift = false;
    if (character.size() != 1)
        return 0;
    const auto it = characterTable().constFind(character.at(0));
    if (it == characterTable().constEnd())
        return 0;
    *needsShift = it->shifted;
    return it->keycode;
}

uint32_t KeyRouter::keycodeForName(const QString &name)
{
    return namedKeyTable().value(name, 0);
}

void KeyRouter::sendText(const QString &text)
{
    if (text.isEmpty())
        return;

    if (m_inputMethod->isActive()) {
        m_inputMethod->commitString(text);
        return;
    }

    // No text input on the other end, so this has to become key events.
    bool needsShift = false;
    const uint32_t keycode = keycodeForCharacter(text, &needsShift);
    if (keycode == 0) {
        // Reachable for anything off the us keymap -- an accented letter from a
        // long-press, an em dash, an emoji -- typed into a client with no
        // text-input-v3, which on this phone means the terminal. wvkbd solves
        // this by compiling a bespoke keymap covering every character its
        // layouts can produce; doing the same here is the obvious next step,
        // and until then this is a logged no-op rather than a wrong character.
        qCWarning(lcRouter) << "no keycode for" << text
                            << "and no active text input; dropped";
        return;
    }

    auto modifiers = m_latched;
    if (needsShift)
        modifiers |= VirtualKeyboard::Shift;
    m_virtualKeyboard->tap(keycode, modifiers);
}

void KeyRouter::sendKey(const QString &name, int modifiers)
{
    const uint32_t keycode = keycodeForName(name);
    if (keycode == 0) {
        qCWarning(lcRouter) << "unknown named key" << name;
        return;
    }
    m_virtualKeyboard->tap(keycode, VirtualKeyboard::Modifiers(modifiers));
}

void KeyRouter::sendChord(const QString &character, int modifiers)
{
    bool needsShift = false;
    const uint32_t keycode = keycodeForCharacter(character.toLower(), &needsShift);
    if (keycode == 0) {
        qCWarning(lcRouter) << "no keycode for chord" << character;
        return;
    }
    m_virtualKeyboard->tap(keycode, VirtualKeyboard::Modifiers(modifiers));
}
