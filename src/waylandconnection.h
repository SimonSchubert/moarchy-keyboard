#pragma once

#include <QObject>
#include <QString>
#include <cstdint>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct zwp_input_method_manager_v2;
struct zwp_virtual_keyboard_manager_v1;

class QSocketNotifier;

// A Wayland connection of our own, separate from the one Qt's platform plugin
// uses to put the panel on screen.
//
// Qt's connection would appear to work: QNativeInterface::QWaylandApplication
// hands out both its wl_display and its wl_seat, and binding our managers on
// them is a few lines shorter than this file. The reason not to is that objects
// created there land on the default event queue, which Qt's platform plugin
// reads on its own event thread and dispatches through machinery that is
// internal, undocumented and has changed between Qt minor releases. The failure
// mode if that changes under us is an input method that silently stops
// receiving `activate` -- a keyboard that never appears, with nothing in the
// log. That is the exact shape of bug this project keeps running into, and it
// is not worth fifty lines.
//
// Two connections from one process is ordinary Wayland, not a trick. Nothing
// crosses between them: the input-method and virtual-keyboard objects live
// here, the layer surface lives on Qt's, and neither references the other.
//
// That separation is only sound because this keyboard does not use
// `get_input_popup_surface`, which would need a wl_surface from *this*
// connection. Long-press alternates are drawn inside the panel instead. If
// popup surfaces are ever wanted, this decision has to be revisited -- do not
// quietly pass Qt's wl_surface into a request made on this connection, which is
// a protocol error and disconnects the client.
class WaylandConnection : public QObject
{
    Q_OBJECT

public:
    explicit WaylandConnection(QObject *parent = nullptr);
    ~WaylandConnection() override;

    // Connects, binds globals, and round-trips until they are all present.
    // Returns false with *error set if the compositor lacks either manager.
    bool open(QString *error);

    wl_display *display() const { return m_display; }
    wl_seat *seat() const { return m_seat; }
    zwp_input_method_manager_v2 *inputMethodManager() const { return m_inputMethodManager; }
    zwp_virtual_keyboard_manager_v1 *virtualKeyboardManager() const { return m_virtualKeyboardManager; }

    void flush();

Q_SIGNALS:
    // The compositor went away or the protocol errored. main() treats this as
    // fatal and exits non-zero, so sway's `exec_always` restarts us (AC 6).
    void lost(const QString &reason);

private:
    void readReady();

    static void handleGlobal(void *data, wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version);
    static void handleGlobalRemove(void *data, wl_registry *registry, uint32_t name);

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    zwp_input_method_manager_v2 *m_inputMethodManager = nullptr;
    zwp_virtual_keyboard_manager_v1 *m_virtualKeyboardManager = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    bool m_lost = false;
};
