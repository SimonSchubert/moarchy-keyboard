#include "inputmethod.h"

#include "waylandconnection.h"

#include <QLoggingCategory>

#include <wayland-client.h>

#include "input-method-unstable-v2-client-protocol.h"

Q_LOGGING_CATEGORY(lcInput, "moarchy.input")

InputMethod::InputMethod(QObject *parent)
    : QObject(parent)
{
}

InputMethod::~InputMethod()
{
    if (m_inputMethod)
        zwp_input_method_v2_destroy(m_inputMethod);
}

void InputMethod::handleActivate(void *data, zwp_input_method_v2 *)
{
    auto *self = static_cast<InputMethod *>(data);
    // A fresh activation resets the pending state, per the protocol: purpose
    // and hint default back to normal/none unless the client sets them again
    // before `done`.
    self->m_pending = State {};
    self->m_pending.active = true;
    Q_EMIT self->activated();
}

void InputMethod::handleDeactivate(void *data, zwp_input_method_v2 *)
{
    static_cast<InputMethod *>(data)->m_pending.active = false;
}

void InputMethod::handleSurroundingText(void *, zwp_input_method_v2 *,
                                        const char *, uint32_t, uint32_t)
{
    // Not used. Backspace goes out as a keycode in both paths rather than as
    // delete_surrounding_text, because the byte counts that request wants are
    // only correct while our copy of the surrounding text is in sync -- and it
    // is not, immediately after a commit_string with no intervening done.
    // A keycode is right in every case and needs no bookkeeping. See
    // KeyRouter::sendNamedKey.
}

void InputMethod::handleTextChangeCause(void *, zwp_input_method_v2 *, uint32_t)
{
}

void InputMethod::handleContentType(void *data, zwp_input_method_v2 *,
                                    uint32_t hint, uint32_t purpose)
{
    auto *self = static_cast<InputMethod *>(data);
    self->m_pending.hint = hint;
    self->m_pending.purpose = static_cast<Purpose>(purpose);
}

void InputMethod::handleDone(void *data, zwp_input_method_v2 *)
{
    auto *self = static_cast<InputMethod *>(data);

    ++self->m_doneCount;

    const State previous = self->m_current;
    self->m_current = self->m_pending;

    if (previous.active != self->m_current.active)
        Q_EMIT self->activeChanged();
    if (previous.purpose != self->m_current.purpose || previous.hint != self->m_current.hint)
        Q_EMIT self->stateChanged();
}

void InputMethod::handleUnavailable(void *data, zwp_input_method_v2 *)
{
    auto *self = static_cast<InputMethod *>(data);
    qCCritical(lcInput) << "another input method already holds this seat";
    Q_EMIT self->unavailable();
}

bool InputMethod::init(WaylandConnection *connection, QString *error)
{
    m_connection = connection;

    static const zwp_input_method_v2_listener listener = {
        &InputMethod::handleActivate,
        &InputMethod::handleDeactivate,
        &InputMethod::handleSurroundingText,
        &InputMethod::handleTextChangeCause,
        &InputMethod::handleContentType,
        &InputMethod::handleDone,
        &InputMethod::handleUnavailable,
    };

    m_inputMethod = zwp_input_method_manager_v2_get_input_method(
        connection->inputMethodManager(), connection->seat());
    if (!m_inputMethod) {
        *error = QStringLiteral("could not obtain a zwp_input_method_v2");
        return false;
    }

    zwp_input_method_v2_add_listener(m_inputMethod, &listener, this);
    connection->flush();
    return true;
}

bool InputMethod::isSensitive() const
{
    if (m_current.purpose == Password || m_current.purpose == Pin)
        return true;
    return (m_current.hint & (HiddenText | SensitiveData)) != 0;
}

void InputMethod::commitString(const QString &text)
{
    if (!m_inputMethod || !m_current.active) {
        qCWarning(lcInput) << "commitString with no active text input; dropping" << text;
        return;
    }

    zwp_input_method_v2_commit_string(m_inputMethod, text.toUtf8().constData());
    zwp_input_method_v2_commit(m_inputMethod, m_doneCount);
    m_connection->flush();
}
