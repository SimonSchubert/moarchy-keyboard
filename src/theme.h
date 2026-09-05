#pragma once

#include <QQmlEngine>
#include <QtGlobal>

#include <QColor>
#include <QHash>
#include <QObject>
#include <QVariantMap>
#include <QString>

class QFileSystemWatcher;
class QTimer;

// The Omarchy palette, read straight from the file omarchy-theme-set writes.
//
// No IPC, no D-Bus, no dependency on the quickshell shell being up: the shell
// and this keyboard are two readers of one file. `omarchy-theme-set` stages a
// copy of the active theme into ~/.local/state/omarchy/current/theme/, and
// colors.toml inside it is a flat `key = "value"` table -- which is why this
// parses it in thirty lines instead of linking a TOML library.
class Theme : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Colors)
    QML_SINGLETON

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool dark READ isDark NOTIFY changed)

    // Panel
    Q_PROPERTY(QColor panelBackground READ panelBackground NOTIFY changed)
    Q_PROPERTY(QColor separator READ separator NOTIFY changed)

    // Character keys
    Q_PROPERTY(QColor keyFill READ keyFill NOTIFY changed)
    Q_PROPERTY(QColor keyText READ keyText NOTIFY changed)
    Q_PROPERTY(QColor keyHint READ keyHint NOTIFY changed)

    // Modifiers, layout switches, backspace -- the non-character keys
    Q_PROPERTY(QColor modifierFill READ modifierFill NOTIFY changed)
    Q_PROPERTY(QColor modifierText READ modifierText NOTIFY changed)

    // Pressed / latched
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)

public:
    // See the note above the class.
    static void setInstance(Theme *instance) { s_instance = instance; }
    static Theme *create(QQmlEngine *, QJSEngine *) {
        // NOT Q_ASSERT: it compiles out under NDEBUG, and the packaged build is
        // Release, so a null instance would be a segfault in the field and a
        // clean abort only on a developer's machine. Returning nullptr makes
        // QML report an unavailable singleton, which is loud and survivable.
        if (!s_instance) {
            qCritical("Theme singleton used before main() set the instance");
            return nullptr;
        }
        // The engine must never delete an object main owns.
        QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
        return s_instance;
    }

    explicit Theme(QObject *parent = nullptr);

    // Watches the live palette and reloads on change. Safe to call when the
    // path does not exist: the built-in palette is used until it appears.
    void start(const QString &colorsPath);

    // Loads one palette with no watching, for offline checks.
    void loadOnce(const QString &colorsPath);

    // Contrast of every text-on-fill pair this theme will actually render,
    // as { "label": ratio }. Used by --check-themes to sweep every Omarchy
    // theme without a phone (AC 25).
    QVariantMap contrastReport() const;

    // How many roles this palette could not supply legibly, so readableOn()
    // substituted black or white. Zero means the theme's own colours were
    // already fine; anything higher means the fallback is load-bearing for it.
    int contrastFallbacks() const { return m_contrastFallbacks; }

    QString name() const { return m_name; }
    bool isDark() const { return m_dark; }

    QColor panelBackground() const { return m_panelBackground; }
    QColor separator() const { return m_separator; }
    QColor keyFill() const { return m_keyFill; }
    QColor keyText() const { return m_keyText; }
    QColor keyHint() const { return m_keyHint; }
    QColor modifierFill() const { return m_modifierFill; }
    QColor modifierText() const { return m_modifierText; }
    QColor accent() const { return m_accent; }
    QColor accentText() const { return m_accentText; }

Q_SIGNALS:
    void changed();

private:
    void reload();
    void rearmWatch();
    static QHash<QString, QString> parseFlatToml(const QString &path, bool *ok);
    static double relativeLuminance(const QColor &color);
    static double contrastRatio(const QColor &a, const QColor &b);
    // AC 25: no theme may render text on a fill it cannot be read against.
    QColor readableOn(const QColor &desired, const QColor &background);

    int m_contrastFallbacks = 0;
    QString m_colorsPath;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;

    QString m_name;
    bool m_dark = true;
    QColor m_panelBackground;
    QColor m_separator;
    QColor m_keyFill;
    QColor m_keyText;
    QColor m_keyHint;
    QColor m_modifierFill;
    QColor m_modifierText;
    QColor m_accent;
    QColor m_accentText;

    inline static Theme *s_instance = nullptr;
};
