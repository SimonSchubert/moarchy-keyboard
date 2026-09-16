#include "chrome.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

#include <cmath>

Q_LOGGING_CATEGORY(lcChrome, "moarchy.chrome")

namespace {

// Ui.js's CORNERS, tile column only. The sheet and card radii are there too and
// deliberately not here: nothing this program draws is a sheet or a card.
int presetTile(const QString &corners)
{
    if (corners == QLatin1String("modest"))
        return 8;
    if (corners == QLatin1String("square"))
        return 0;
    return 20;
}

// Ui.js's normCorners, aliases and all, so a file `moarchy-ui` accepts means the
// same thing here. Anything unrecognised is `large`, as it is there.
QString normCorners(const QString &value)
{
    const QString n = value.trimmed().toLower();
    if (n == QLatin1String("none") || n == QLatin1String("off")
        || n == QLatin1String("flat") || n == QLatin1String("0")
        || n == QLatin1String("square"))
        return QStringLiteral("square");
    if (n == QLatin1String("small") || n == QLatin1String("little")
        || n == QLatin1String("soft") || n == QLatin1String("modest"))
        return QStringLiteral("modest");
    return QStringLiteral("large");
}

// Ui.js's parseToml. Not Theme::parseFlatToml, which stops at the first table
// header: this one skips headers and reads on, and strips a trailing comment,
// because that is what the shell does with the same file.
QHash<QString, QString> parseUiToml(const QString &path, bool *ok)
{
    QHash<QString, QString> values;
    *ok = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;
    *ok = true;

    static const QRegularExpression trailingComment(QStringLiteral("\\s+#.*$"));
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().remove(trailingComment).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('[')))
            continue;

        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 1)
            continue;

        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2
            && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))))
            value = value.mid(1, value.size() - 2);

        values.insert(key, value);
    }
    return values;
}

} // namespace

Chrome::Chrome(QObject *parent)
    : QObject(parent)
{
}

void Chrome::start(const QString &path)
{
    m_path = path;

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

void Chrome::loadOnce(const QString &path)
{
    m_path = path;
    reload();
}

void Chrome::rearmWatch()
{
    // Theme::rearmWatch's two halves, for AC 60 rather than AC 24: the file for
    // an in-place write, which is what `moarchy-ui` does, and the directory for
    // a replacement or for the file appearing at all -- a phone that has never
    // picked a corner style has no ui.toml, and the first pick creates it.
    const QString directory = QFileInfo(m_path).absolutePath();

    if (!m_watcher->directories().contains(directory) && QDir(directory).exists())
        m_watcher->addPath(directory);

    if (!m_watcher->files().contains(m_path) && QFile::exists(m_path))
        m_watcher->addPath(m_path);
}

void Chrome::reload()
{
    bool ok = false;
    const QHash<QString, QString> values = parseUiToml(m_path, &ok);

    const QString corners = normCorners(values.value(QStringLiteral("corners")));

    // Ui.js's num(): a present, finite, non-negative number wins over the
    // preset, and anything else -- empty, negative, not a number -- is ignored
    // rather than read as zero. A typo must not square the handle off.
    int tile = presetTile(corners);
    const QString raw = values.value(QStringLiteral("tile")).trimmed();
    if (!raw.isEmpty()) {
        bool numeric = false;
        const double parsed = raw.toDouble(&numeric);
        if (numeric && std::isfinite(parsed) && parsed >= 0)
            tile = qRound(parsed);
    }

    if (!ok)
        qCInfo(lcChrome) << "no" << m_path << "-- corners are large";

    // A watch on a directory fires for every file in it, so most events change
    // nothing. Logged once regardless, so the first reading is on record.
    if (m_loaded && corners == m_corners && tile == m_tile)
        return;

    m_loaded = true;
    m_corners = corners;
    m_tile = tile;
    qCInfo(lcChrome) << "corners" << m_corners << "tile" << m_tile;
    Q_EMIT changed();
}
