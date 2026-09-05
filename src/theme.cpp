#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QTextStream>
#include <QTimer>

#include <cmath>

Q_LOGGING_CATEGORY(lcTheme, "moarchy.theme")

namespace {

// Catppuccin Mocha, which is what Omarchy ships as its default. Used only when
// colors.toml is missing or unreadable -- never silently, always with a log
// line, and never leaving the keyboard unpainted (AC 27).
constexpr const char *kFallbackBackground = "#1e1e2e";
constexpr const char *kFallbackLighter = "#313244";
constexpr const char *kFallbackForeground = "#cdd6f4";
constexpr const char *kFallbackMuted = "#585b70";
constexpr const char *kFallbackSelection = "#45475a";
constexpr const char *kFallbackAccent = "#89b4fa";

// WCAG AA for normal-size text. Applied to every text-on-fill pair the keyboard
// draws, hints included: a hint you cannot read is not subtle, it is absent.
constexpr double kMinimumContrast = 4.5;

} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
{
}

void Theme::start(const QString &colorsPath)
{
    m_colorsPath = colorsPath;

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(60);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        rearmWatch();
        reload();
    });

    m_watcher = new QFileSystemWatcher(this);
    const auto queue = [this] { m_debounce->start(); };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, queue);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, queue);

    rearmWatch();
    reload();
}

void Theme::loadOnce(const QString &colorsPath)
{
    m_colorsPath = colorsPath;
    reload();
}

QVariantMap Theme::contrastReport() const
{
    // Every pair that actually gets drawn. A role that is never rendered on a
    // given fill does not belong here -- a report full of pairs the program
    // cannot produce would fail themes for no reason.
    QVariantMap report;
    report.insert(QStringLiteral("key text on key fill"),
                  contrastRatio(m_keyText, m_keyFill));
    report.insert(QStringLiteral("modifier text on modifier fill"),
                  contrastRatio(m_modifierText, m_modifierFill));
    report.insert(QStringLiteral("accent text on accent"),
                  contrastRatio(m_accentText, m_accent));
    report.insert(QStringLiteral("long-press hint on key fill"),
                  contrastRatio(m_keyHint, m_keyFill));
    return report;
}

void Theme::rearmWatch()
{
    // omarchy-theme-set REPLACES colors.toml rather than editing it in place --
    // it stages a whole new copy of the theme directory. An inotify watch is on
    // the inode, so the first replacement fires one signal and then watches a
    // file nobody will ever write to again. Every subsequent theme change is
    // silent.
    //
    // The fix is both halves: re-add the file path after every event, and watch
    // the containing directory too, so a replacement is still noticed while the
    // file path momentarily does not exist. Testing this needs THREE theme
    // changes -- with only one, the broken version passes (AC 24).
    const QString directory = QFileInfo(m_colorsPath).absolutePath();

    if (!m_watcher->directories().contains(directory) && QDir(directory).exists())
        m_watcher->addPath(directory);

    if (!m_watcher->files().contains(m_colorsPath) && QFile::exists(m_colorsPath))
        m_watcher->addPath(m_colorsPath);
}

QHash<QString, QString> Theme::parseFlatToml(const QString &path, bool *ok)
{
    QHash<QString, QString> values;
    *ok = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        // colors.toml is flat, but stop at a table header rather than reading
        // keys out of one as though they were top level.
        if (line.startsWith(QLatin1Char('[')))
            break;

        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;

        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))))
            value = value.mid(1, value.size() - 2);

        values.insert(key, value);
    }

    *ok = !values.isEmpty();
    return values;
}

double Theme::relativeLuminance(const QColor &color)
{
    auto channel = [](double c) {
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF())
         + 0.7152 * channel(color.greenF())
         + 0.0722 * channel(color.blueF());
}

double Theme::contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor Theme::roleColor(const QString &key) const
{
    return QColor(m_raw.value(key));
}

QColor Theme::composite(const QColor &foreground, const QColor &background, double alpha)
{
    // Straight source-over. Contrast has to be measured on what is actually
    // drawn, and a translucent foreground is never the colour it was written
    // as -- which is why an alpha that reads fine on the base background can
    // fail on a raised card a few percent lighter.
    return QColor::fromRgbF(
        background.redF()   + (foreground.redF()   - background.redF())   * alpha,
        background.greenF() + (foreground.greenF() - background.greenF()) * alpha,
        background.blueF()  + (foreground.blueF()  - background.blueF())  * alpha);
}

QColor Theme::readableOn(const QColor &desired, const QColor &background)
{
    if (contrastRatio(desired, background) >= kMinimumContrast)
        return desired;

    ++m_contrastFallbacks;

    // Every one of the 22 Omarchy themes trips this for at least one role, so
    // it is load-bearing rather than a safety net for odd themes. Which is
    // exactly why it must not be a sledgehammer: substituting pure black or
    // white made the long-press hints -- deliberately quiet, small, secondary
    // -- shout in white on every single theme, and threw the palette's hue away
    // to fix a legibility problem the palette was only slightly short of.
    //
    // So walk the colour toward whichever extreme this background contrasts
    // with, and stop the moment it clears AA. A theme that was nearly legible
    // moves barely at all and keeps its hue; one that was hopeless ends up at
    // the extreme, which is where the old code started.
    const QColor target = contrastRatio(Qt::white, background)
                              >= contrastRatio(Qt::black, background)
                          ? QColor(Qt::white) : QColor(Qt::black);

    QColor best = target;                      // guaranteed-legible fallback
    for (int step = 1; step <= 20; ++step) {
        const double t = step / 20.0;
        const QColor blended = QColor::fromRgbF(
            desired.redF()   + (target.redF()   - desired.redF())   * t,
            desired.greenF() + (target.greenF() - desired.greenF()) * t,
            desired.blueF()  + (target.blueF()  - desired.blueF())  * t);
        if (contrastRatio(blended, background) >= kMinimumContrast) {
            best = blended;
            break;
        }
    }

    qCDebug(lcTheme) << "contrast" << contrastRatio(desired, background)
                     << "too low for" << desired.name() << "on" << background.name()
                     << "-> nudged to" << best.name()
                     << "at" << contrastRatio(best, background);
    return best;
}

void Theme::reload()
{
    m_contrastFallbacks = 0;

    bool ok = false;
    const QHash<QString, QString> values = parseFlatToml(m_colorsPath, &ok);

    if (!ok) {
        qCWarning(lcTheme) << "could not read" << m_colorsPath
                           << "-- falling back to the built-in palette";
    }

    m_raw = values;

    auto colorFor = [&values](const QString &key, const char *fallback) {
        const QColor parsed(values.value(key));
        return parsed.isValid() ? parsed : QColor(QString::fromLatin1(fallback));
    };

    m_dark = values.value(QStringLiteral("mode"), QStringLiteral("dark")) != QLatin1String("light");

    const QColor background = colorFor(QStringLiteral("background"), kFallbackBackground);
    const QColor lighter = colorFor(QStringLiteral("lighter_background"), kFallbackLighter);
    const QColor foreground = colorFor(QStringLiteral("foreground"), kFallbackForeground);
    const QColor muted = colorFor(QStringLiteral("muted"), kFallbackMuted);
    const QColor selection = colorFor(QStringLiteral("selection"), kFallbackSelection);
    const QColor accent = colorFor(QStringLiteral("accent"), kFallbackAccent);

    m_panelBackground = background;
    m_keyFill = lighter;
    m_modifierFill = selection;
    m_accent = accent;

    m_keyText = readableOn(foreground, m_keyFill);
    m_modifierText = readableOn(foreground, m_modifierFill);
    m_accentText = readableOn(background, m_accent);

    // Hints (the long-press alternate shown small in a key's corner) are
    // deliberately quiet, but must still clear AA against the key fill.
    m_keyHint = readableOn(muted, m_keyFill);

    m_separator = background.lighter(m_dark ? 130 : 90);

    const QFileInfo info(m_colorsPath);
    QFile nameFile(info.absolutePath() + QStringLiteral("/../theme.name"));
    if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
        m_name = QString::fromUtf8(nameFile.readAll()).trimmed();

    qCInfo(lcTheme) << "palette" << (m_name.isEmpty() ? QStringLiteral("(unnamed)") : m_name)
                    << (m_dark ? "dark" : "light") << "bg" << m_panelBackground.name()
                    << "key" << m_keyFill.name() << "text" << m_keyText.name();

    Q_EMIT changed();
}
