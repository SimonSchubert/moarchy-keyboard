#pragma once

#include "qmlsingleton.h"

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
class Theme : public QObject, public MainOwnedSingleton<Theme>
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
    // NO default argument on the constructor, deliberately.
    //
    // With `QObject *parent = nullptr` this class is default-constructible, and
    // the QML engine then builds its OWN instance rather than calling create().
    // It does that silently: no warning, no error, and create() -- along with
    // any check inside it -- is simply never called. The result here was a
    // keyboard that drew every surface at #000000, because QML held a freshly
    // constructed Theme whose QColor members were all default-invalid while
    // main held the configured one. Black on black, no diagnostics, and it
    // took a pixel sample and an address comparison to see it.
    //
    // Removing the default argument makes the type non-default-constructible,
    // which leaves create() as the only way the engine can obtain it.
    explicit Theme(QObject *parent);

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

    // Exposed for --check-contrast, so anything else in mobileomarchy can check
    // a colour pair against the same maths the keyboard uses rather than a
    // second implementation that might round differently.
    static double contrastOf(const QColor &a, const QColor &b) { return contrastRatio(a, b); }
    static QColor composite(const QColor &foreground, const QColor &background, double alpha);

    // Mix two opaque colours. Exposed to QML because the shell's Util.alpha
    // idiom is not available here -- this is a separate process with its own
    // QML module -- and because a border wants a colour derived from the theme
    // rather than a hardcoded one that survives exactly one palette.
    Q_INVOKABLE QColor blend(const QColor &a, const QColor &b, double t) const
    {
        return composite(a, b, t);
    }

    // The palette exactly as colors.toml declared it, before any of this
    // class's role mapping. Lets --check-themes measure an arbitrary pair of
    // theme roles, which is what anything else drawing on an Omarchy surface
    // actually needs -- checking one theme and assuming the other 21 is how a
    // subtitle ends up illegible on half the phone.
    QColor roleColor(const QString &key) const;

    // Resolves a colour written as one of:
    //
    //   foreground                    a role name from colors.toml
    //   #rrggbb                       a literal
    //   mix(background,foreground,0.08)   a role composited over another
    //
    // The mix() form exists because two role names cannot describe a raised
    // card. A surface painted as `alpha(foreground, 0.08)` over `background` is
    // neither role, and measuring against either one alone under-reports: on
    // Catppuccin the difference between the composite and lighter_background is
    // 4.50:1 against 5.44:1, which is the difference between failing AA and
    // thinking you passed.
    QColor resolveColor(const QString &expression, bool *ok) const;

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

    QHash<QString, QString> m_raw;
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

};

// See MainOwnedSingleton in qmlsingleton.h: a default-constructible
// QML_SINGLETON is built by the engine instead of by create(), and QML then
// holds a different object from the one main() configured. That shipped once
// and drew every surface black, so it is a build error instead.
MOARCHY_SINGLETON_INVARIANT(Theme);
