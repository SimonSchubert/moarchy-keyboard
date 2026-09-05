#include "waylandconnection.h"

#include "startupclock.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QThread>

#include <wayland-client.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

WaylandConnection::WaylandConnection(QObject *parent)
    : QObject(parent)
{
}

WaylandConnection::~WaylandConnection()
{
    if (m_inputMethodManager)
        zwp_input_method_manager_v2_destroy(m_inputMethodManager);
    if (m_virtualKeyboardManager)
        zwp_virtual_keyboard_manager_v1_destroy(m_virtualKeyboardManager);
    if (m_seat)
        wl_seat_destroy(m_seat);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_disconnect(m_display);
}

void WaylandConnection::handleGlobal(void *data, wl_registry *registry, uint32_t name,
                                     const char *interface, uint32_t version)
{
    auto *self = static_cast<WaylandConnection *>(data);

    if (qstrcmp(interface, wl_seat_interface.name) == 0) {
        // Only the first seat. This phone has one, and an input method is bound
        // per-seat anyway.
        if (!self->m_seat) {
            self->m_seat = static_cast<wl_seat *>(
                wl_registry_bind(registry, name, &wl_seat_interface, qMin(version, 7u)));
        }
    } else if (qstrcmp(interface, zwp_input_method_manager_v2_interface.name) == 0) {
        self->m_inputMethodManager = static_cast<zwp_input_method_manager_v2 *>(
            wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1));
    } else if (qstrcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        self->m_virtualKeyboardManager = static_cast<zwp_virtual_keyboard_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
    }
}

void WaylandConnection::handleGlobalRemove(void *, wl_registry *, uint32_t)
{
}

bool WaylandConnection::open(QString *error)
{
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        *error = QStringLiteral("cannot connect to the Wayland display "
                                "(is WAYLAND_DISPLAY set?)");
        return false;
    }

    // Declared here rather than at namespace scope so it can name the private
    // static handlers: a static local inside a member function is initialised
    // in this function's access context.
    static const wl_registry_listener listener = {
        &WaylandConnection::handleGlobal,
        &WaylandConnection::handleGlobalRemove,
    };

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &listener, this);

    // Two round trips: the first delivers the global advertisements, the second
    // any events the binds themselves produce.
    wl_display_roundtrip(m_display);
    wl_display_roundtrip(m_display);
    MOARCHY_MARK("wayland globals bound");

    if (!m_seat) {
        *error = QStringLiteral("compositor advertises no wl_seat");
        return false;
    }
    if (!m_inputMethodManager) {
        *error = QStringLiteral(
            "compositor does not advertise zwp_input_method_manager_v2 -- "
            "an on-screen keyboard cannot work without it");
        return false;
    }
    if (!m_virtualKeyboardManager) {
        *error = QStringLiteral(
            "compositor does not advertise zwp_virtual_keyboard_manager_v1 -- "
            "keys could be committed as text but never as keycodes, so nothing "
            "outside a text-input-v3 client could be typed into");
        return false;
    }

    m_notifier = new QSocketNotifier(wl_display_get_fd(m_display), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &WaylandConnection::readReady);

    // Requests are queued in a client-side buffer and only reach the compositor
    // on flush. Without this, a commit_string sits in the buffer until the next
    // unrelated event wakes us -- which reads as a keyboard that types a
    // character late, or not until you press the next key.
    connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock,
            this, &WaylandConnection::flush);

    return true;
}

void WaylandConnection::flush()
{
    if (m_display && !m_lost)
        wl_display_flush(m_display);
}

void WaylandConnection::readReady()
{
    if (m_lost)
        return;

    // The prepare_read / read_events dance rather than a bare wl_display_dispatch,
    // which can block if the notifier fires spuriously and there is nothing to
    // read. We are the only user of this connection, so the loop below always
    // terminates.
    while (wl_display_prepare_read(m_display) != 0) {
        if (wl_display_dispatch_pending(m_display) < 0) {
            m_lost = true;
            Q_EMIT lost(QStringLiteral("dispatch failed: %1").arg(qt_error_string(errno)));
            return;
        }
    }

    wl_display_flush(m_display);

    if (wl_display_read_events(m_display) < 0) {
        m_lost = true;
        Q_EMIT lost(QStringLiteral("read failed: %1").arg(qt_error_string(errno)));
        return;
    }

    if (wl_display_dispatch_pending(m_display) < 0) {
        m_lost = true;
        Q_EMIT lost(QStringLiteral("dispatch failed: %1").arg(qt_error_string(errno)));
    }
}
