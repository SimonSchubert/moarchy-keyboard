#include "virtualkeyboard.h"

#include "waylandconnection.h"

#include <QLoggingCategory>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

#include "virtual-keyboard-unstable-v1-client-protocol.h"

Q_LOGGING_CATEGORY(lcKeys, "moarchy.keys")

VirtualKeyboard::VirtualKeyboard(QObject *parent)
    : QObject(parent)
{
}

VirtualKeyboard::~VirtualKeyboard()
{
    if (m_keyboard)
        zwp_virtual_keyboard_v1_destroy(m_keyboard);
}

bool VirtualKeyboard::init(WaylandConnection *connection, QString *error)
{
    m_connection = connection;
    m_clock.start();

    m_keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        connection->virtualKeyboardManager(), connection->seat());
    if (!m_keyboard) {
        *error = QStringLiteral("could not create a virtual keyboard");
        return false;
    }

    xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        *error = QStringLiteral("xkb_context_new failed");
        return false;
    }

    // Plain us. Every character the layouts can reach through a *keycode* has
    // to exist here; anything outside it (accented letters, dashes, emoji) is
    // reachable only through the text path. See KeyRouter::sendText.
    xkb_rule_names names {};
    names.layout = "us";

    xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        xkb_context_unref(context);
        *error = QStringLiteral("could not compile the 'us' xkb keymap");
        return false;
    }

    auto maskFor = [keymap](const char *name) -> uint32_t {
        const xkb_mod_index_t index = xkb_keymap_mod_get_index(keymap, name);
        return index == XKB_MOD_INVALID ? 0u : (1u << index);
    };
    m_shiftMask = maskFor(XKB_MOD_NAME_SHIFT);
    m_controlMask = maskFor(XKB_MOD_NAME_CTRL);
    m_altMask = maskFor(XKB_MOD_NAME_ALT);
    m_superMask = maskFor(XKB_MOD_NAME_LOGO);
    m_altGrMask = maskFor("Mod5");

    char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    if (!text) {
        *error = QStringLiteral("could not serialise the xkb keymap");
        return false;
    }

    // The compositor mmaps this, so the size must include the terminating NUL.
    const size_t size = std::strlen(text) + 1;

    const int fd = memfd_create("moarchy-keymap", MFD_CLOEXEC);
    if (fd < 0) {
        free(text);
        *error = QStringLiteral("memfd_create failed: %1").arg(qt_error_string(errno));
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        free(text);
        close(fd);
        *error = QStringLiteral("ftruncate on the keymap fd failed");
        return false;
    }

    void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        free(text);
        close(fd);
        *error = QStringLiteral("mmap of the keymap fd failed");
        return false;
    }
    std::memcpy(map, text, size);
    munmap(map, size);
    free(text);

    zwp_virtual_keyboard_v1_keymap(m_keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                   fd, static_cast<uint32_t>(size));
    close(fd);
    m_connection->flush();

    return true;
}

uint32_t VirtualKeyboard::timestamp()
{
    return static_cast<uint32_t>(m_clock.elapsed());
}

uint32_t VirtualKeyboard::xkbMask(Modifiers modifiers) const
{
    uint32_t mask = 0;
    if (modifiers.testFlag(Shift))
        mask |= m_shiftMask;
    if (modifiers.testFlag(Control))
        mask |= m_controlMask;
    if (modifiers.testFlag(Alt))
        mask |= m_altMask;
    if (modifiers.testFlag(Super))
        mask |= m_superMask;
    if (modifiers.testFlag(AltGr))
        mask |= m_altGrMask;
    return mask;
}

void VirtualKeyboard::sendModifiers(Modifiers modifiers)
{
    if (!m_keyboard)
        return;
    const uint32_t mask = xkbMask(modifiers);
    zwp_virtual_keyboard_v1_modifiers(m_keyboard, mask, 0, 0, 0);
}

void VirtualKeyboard::setHeldModifiers(Modifiers modifiers)
{
    m_held = modifiers;
    sendModifiers(m_held);
    m_connection->flush();
}

void VirtualKeyboard::tap(uint32_t linuxKeycode, Modifiers modifiers)
{
    if (!m_keyboard) {
        qCWarning(lcKeys) << "no virtual keyboard; dropping keycode" << linuxKeycode;
        return;
    }

    const Modifiers effective = m_held | modifiers;

    sendModifiers(effective);
    zwp_virtual_keyboard_v1_key(m_keyboard, timestamp(), linuxKeycode,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(m_keyboard, timestamp(), linuxKeycode,
                                WL_KEYBOARD_KEY_STATE_RELEASED);

    // Drop anything that was only held for this key, but keep what the user
    // latched deliberately.
    if (effective != m_held)
        sendModifiers(m_held);

    m_connection->flush();
}
