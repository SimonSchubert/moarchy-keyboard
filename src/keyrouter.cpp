#include "keyrouter.h"

#include "inputmethod.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcRouter, "moarchy.router")


KeyRouter::KeyRouter(InputMethod *inputMethod, VirtualKeyboard *virtualKeyboard, QObject *parent)
    : QObject(parent)
    , m_inputMethod(inputMethod)
    , m_virtualKeyboard(virtualKeyboard)
{
    connect(m_inputMethod, &InputMethod::activeChanged, this, &KeyRouter::activeChanged);
    connect(m_inputMethod, &InputMethod::stateChanged, this, &KeyRouter::stateChanged);
}

void KeyRouter::markPress()
{
    m_sincePress.start();
    m_frameReported = false;
}

void KeyRouter::noteFrame()
{
    // Only the FIRST frame after a press is the feedback frame; every later one
    // is unrelated and would make the number look better than it is.
    if (m_frameReported || !m_sincePress.isValid())
        return;
    m_frameReported = true;
    qCInfo(lcRouter) << "latency: press to first frame"
                     << m_sincePress.elapsed() << "ms";
}

void KeyRouter::reportLatency(const char *what, const QString &detail)
{
    if (!m_sincePress.isValid())
        return;
    qCInfo(lcRouter).noquote() << QStringLiteral("latency: press to %1 %2 ms  (%3)")
                                     .arg(QLatin1String(what))
                                     .arg(m_sincePress.elapsed())
                                     .arg(detail);
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

void KeyRouter::sendText(const QString &text)
{
    if (text.isEmpty())
        return;

    if (m_inputMethod->isActive()) {
        m_inputMethod->commitString(text);
        reportLatency("commit_string on the wire", text);
        return;
    }

    // No text input on the other end, so this has to become key events.
    bool needsShift = false;
    const uint32_t keycode = m_virtualKeyboard->keycodeForCharacter(text, &needsShift);
    if (keycode == 0) {
        // Now only reachable for something the generated keymap could not
        // cover: a multi-code-unit string (an emoji), or a character that
        // arrived after the keymap was built. Everything the layouts declare at
        // startup has a keycode -- see VirtualKeyboard::buildKeymap.
        qCWarning(lcRouter) << "no keycode for" << text
                            << "and no active text input; dropped";
        return;
    }

    m_virtualKeyboard->tap(keycode,
                           needsShift ? VirtualKeyboard::Modifiers(VirtualKeyboard::Shift)
                                      : VirtualKeyboard::Modifiers(VirtualKeyboard::NoModifiers));
    reportLatency("keycode on the wire", text);
}

void KeyRouter::sendKey(const QString &name, int modifiers)
{
    const uint32_t keycode = VirtualKeyboard::keycodeForName(name);
    if (keycode == 0) {
        qCWarning(lcRouter) << "unknown named key" << name;
        return;
    }
    m_virtualKeyboard->tap(keycode, VirtualKeyboard::Modifiers(modifiers));
    reportLatency("keycode on the wire", name);
}

void KeyRouter::sendChord(const QString &character, int modifiers)
{
    bool needsShift = false;
    const uint32_t keycode = m_virtualKeyboard->keycodeForCharacter(character.toLower(), &needsShift);
    if (keycode == 0) {
        qCWarning(lcRouter) << "no keycode for chord" << character;
        return;
    }
    m_virtualKeyboard->tap(keycode, VirtualKeyboard::Modifiers(modifiers));
}
