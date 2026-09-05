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

namespace {

struct BaseKey {
    uint32_t keycode;   // evdev
    bool shifted;
};

// The us layout, written as the four rows it actually is, in evdev order, so
// the plain and shifted tables cannot drift apart the way two hand-maintained
// lists would.
const QHash<QChar, BaseKey> &baseTable()
{
    static const QHash<QChar, BaseKey> table = [] {
        QHash<QChar, BaseKey> map;
        struct Row { const char *plain; const char *shift; uint32_t first; };
        static const Row rows[] = {
            { "1234567890-=",  "!@#$%^&*()_+",  2  },
            { "qwertyuiop[]",  "QWERTYUIOP{}",  16 },
            { "asdfghjkl;'`",  "ASDFGHJKL:\"~", 30 },
            { "\\zxcvbnm,./",  "|ZXCVBNM<>?",   43 },
        };
        for (const Row &row : rows) {
            const QString plain = QString::fromLatin1(row.plain);
            const QString shift = QString::fromLatin1(row.shift);
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

// Spare xkb keycodes. The evdev keycodes file declares <I200>..<I255>, which
// nothing on this hardware emits, so they are free to redefine. xkb keycode
// minus 8 is the evdev code that goes on the wire.
constexpr uint32_t kFirstSpareXkb = 200;
constexpr uint32_t kLastSpareXkb = 255;

} // namespace

VirtualKeyboard::VirtualKeyboard(QObject *parent)
    : QObject(parent)
{
}

VirtualKeyboard::~VirtualKeyboard()
{
    if (m_keyboard)
        zwp_virtual_keyboard_v1_destroy(m_keyboard);
}

QString VirtualKeyboard::buildKeymap(const QStringList &characters)
{
    // Collect what us cannot reach, in a stable order so the same layouts
    // always produce the same keymap.
    QStringList extras;
    for (const QString &character : characters) {
        if (character.size() != 1)
            continue;                       // multi-code-unit: text path only
        const QChar ch = character.at(0);
        if (baseTable().contains(ch))
            continue;
        if (m_generated.contains(ch))
            continue;
        if (kFirstSpareXkb + uint32_t(m_generated.size()) > kLastSpareXkb) {
            qCWarning(lcKeys) << "ran out of spare keycodes; these can only be"
                              << "typed through the text path from now on:" << character;
            continue;
        }
        const uint32_t xkbCode = kFirstSpareXkb + uint32_t(m_generated.size());
        m_generated.insert(ch, xkbCode - 8);

        // Only the hex digits are uppercased. Uppercasing the whole line would
        // turn `key` into `KEY`, and xkb's grammar will not have it.
        const QString hex = QStringLiteral("%1")
                                .arg(uint(ch.unicode()), 4, 16, QLatin1Char('0'))
                                .toUpper();
        extras.append(QStringLiteral("    key <I%1> { [ U%2 ] };").arg(xkbCode).arg(hex));
    }

    // `UXXXX` is xkbcommon's Unicode keysym spelling. Using it uniformly avoids
    // having to find a name for every symbol (EuroSign, emdash, ...) and works
    // for anything in the BMP.
    return QStringLiteral(
               "xkb_keymap {\n"
               "  xkb_keycodes { include \"evdev\" };\n"
               "  xkb_types    { include \"complete\" };\n"
               "  xkb_compat   { include \"complete\" };\n"
               "  xkb_symbols  { include \"pc+us+inet(evdev)\"\n"
               "%1\n"
               "  };\n"
               "};\n")
        .arg(extras.join(QLatin1Char('\n')));
}

QString VirtualKeyboard::keymapSource(const QStringList &characters, QString *compileError)
{
    compileError->clear();
    m_generated.clear();

    const QString source = buildKeymap(characters);

    xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        *compileError = QStringLiteral("xkb_context_new failed");
        return source;
    }

    const QByteArray utf8 = source.toUtf8();
    xkb_keymap *keymap = xkb_keymap_new_from_string(
        context, utf8.constData(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (!keymap)
        *compileError = QStringLiteral("xkb refused the generated keymap");
    else
        xkb_keymap_unref(keymap);

    xkb_context_unref(context);
    return source;
}

bool VirtualKeyboard::init(WaylandConnection *connection, const QStringList &characters,
                           QString *error)
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

    const QByteArray source = buildKeymap(characters).toUtf8();

    xkb_keymap *keymap = xkb_keymap_new_from_string(
        context, source.constData(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (!keymap) {
        // Falling back rather than dying: a keyboard that can type ASCII beats
        // no keyboard, and the generated tail is the only part at risk.
        qCWarning(lcKeys) << "generated keymap did not compile; falling back to plain us."
                          << m_generated.size() << "characters will be text-path only";
        m_generated.clear();
        xkb_rule_names names {};
        names.layout = "us";
        keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    if (!keymap) {
        xkb_context_unref(context);
        *error = QStringLiteral("could not compile any xkb keymap");
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

    qCInfo(lcKeys) << "keymap uploaded:" << size << "bytes, us plus"
                   << m_generated.size() << "generated keys";
    return true;
}

uint32_t VirtualKeyboard::keycodeForCharacter(const QString &character, bool *needsShift) const
{
    *needsShift = false;
    if (character.size() != 1)
        return 0;
    const QChar ch = character.at(0);

    const auto base = baseTable().constFind(ch);
    if (base != baseTable().constEnd()) {
        *needsShift = base->shifted;
        return base->keycode;
    }
    return m_generated.value(ch, 0);
}

uint32_t VirtualKeyboard::keycodeForName(const QString &name)
{
    static const QHash<QString, uint32_t> table = {
        { QStringLiteral("Escape"),    1   }, { QStringLiteral("BackSpace"), 14  },
        { QStringLiteral("Tab"),       15  }, { QStringLiteral("Return"),    28  },
        { QStringLiteral("space"),     57  }, { QStringLiteral("Home"),      102 },
        { QStringLiteral("Up"),        103 }, { QStringLiteral("PageUp"),    104 },
        { QStringLiteral("Left"),      105 }, { QStringLiteral("Right"),     106 },
        { QStringLiteral("End"),       107 }, { QStringLiteral("Down"),      108 },
        { QStringLiteral("PageDown"),  109 }, { QStringLiteral("Insert"),    110 },
        { QStringLiteral("Delete"),    111 },
    };
    return table.value(name, 0);
}

uint32_t VirtualKeyboard::timestamp()
{
    return static_cast<uint32_t>(m_clock.elapsed());
}

uint32_t VirtualKeyboard::xkbMask(Modifiers modifiers) const
{
    uint32_t mask = 0;
    if (modifiers.testFlag(Shift))   mask |= m_shiftMask;
    if (modifiers.testFlag(Control)) mask |= m_controlMask;
    if (modifiers.testFlag(Alt))     mask |= m_altMask;
    if (modifiers.testFlag(Super))   mask |= m_superMask;
    if (modifiers.testFlag(AltGr))   mask |= m_altGrMask;
    return mask;
}

void VirtualKeyboard::sendModifiers(Modifiers modifiers)
{
    if (!m_keyboard)
        return;
    zwp_virtual_keyboard_v1_modifiers(m_keyboard, xkbMask(modifiers), 0, 0, 0);
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

    // Drop anything held only for this key, but keep what the user latched.
    if (effective != m_held)
        sendModifiers(m_held);

    m_connection->flush();
}
