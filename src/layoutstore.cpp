#include "layoutstore.h"

#include "layoutparser.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSet>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcLayouts, "moarchy.layouts")

LayoutStore::LayoutStore(QObject *parent)
    : QObject(parent)
{
    m_searchPaths = {
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
            + QStringLiteral("/moarchy-keyboard/layouts"),
        QStringLiteral(MOARCHY_DATADIR "/moarchy-keyboard/layouts"),
        QStringLiteral(":/layouts"),
    };
    reload();
}

void LayoutStore::reload()
{
    m_layouts.clear();
    m_problems.clear();
    m_names.clear();

    // Which file each name resolves to. First path wins, so a user layout
    // shadows the shipped one of the same name rather than merging with it --
    // and, just as importantly, a BROKEN user layout does not silently fall
    // through to the shipped one. An edit that looks like it did nothing is
    // worse than an edit that says why it failed (AC 19).
    QStringList names;
    QHash<QString, QString> paths;
    for (const QString &directory : std::as_const(m_searchPaths)) {
        QDir dir(directory);
        if (!dir.exists())
            continue;
        for (const QFileInfo &info :
             dir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files)) {
            const QString name = info.completeBaseName();
            if (paths.contains(name))
                continue;
            paths.insert(name, info.absoluteFilePath());
            names.append(name);
        }
    }

    names.sort();
    m_names = names;

    // Parsed here rather than on first use. Nothing is loaded lazily in
    // practice anyway -- main() calls allCharacters() before the QML engine
    // exists, to compile the keymap from every character the layouts declare --
    // and parsing once, up front, is what lets layout() be a const lookup
    // cheap enough for a QML binding to call.
    for (const QString &name : std::as_const(m_names)) {
        QFile file(paths.value(name));
        QStringList problems;
        if (!file.open(QIODevice::ReadOnly)) {
            problems.append(QStringLiteral("could not be opened"));
            qCWarning(lcLayouts) << "cannot read" << paths.value(name);
            m_problems.insert(name, problems);
            m_layouts.insert(name, LayoutSpec {});
            continue;
        }

        const LayoutSpec spec = LayoutParser::parse(file.readAll(), name, &problems);
        if (!problems.isEmpty()) {
            qCWarning(lcLayouts).noquote()
                << paths.value(name) << "--" << problems.join(QLatin1String("; "));
        }
        m_layouts.insert(name, spec);
        m_problems.insert(name, problems);
        qCDebug(lcLayouts) << "loaded" << name << "from" << paths.value(name)
                           << "--" << spec.rows().size() << "rows";
    }

    Q_EMIT changed();
}

LayoutSpec LayoutStore::layout(const QString &name) const
{
    const auto found = m_layouts.constFind(name);
    if (found != m_layouts.constEnd())
        return *found;

    qCWarning(lcLayouts) << "no layout named" << name << "on any search path";
    return {};
}

QStringList LayoutStore::problems(const QString &name) const
{
    return m_problems.value(name);
}

QStringList LayoutStore::allCharacters() const
{
    QSet<QString> found;

    for (const QString &name : m_names) {
        const LayoutSpec spec = m_layouts.value(name);
        for (const KeyRowSpec &row : spec.rows()) {
            for (const KeySpec &key : row.keys()) {
                if (key.text().size() == 1)
                    found.insert(key.text());

                for (const QString &alternate : key.alt()) {
                    if (alternate.size() == 1)
                        found.insert(alternate);
                }
            }
        }
    }

    QStringList characters(found.begin(), found.end());
    // Sorted so the same layouts always produce the same keymap, which makes a
    // keycode reproducible between runs and a bug report worth something.
    characters.sort();
    return characters;
}

QUrl LayoutStore::iconFontUrl() const
{
    // Omarchy is vendored to a path every omarchy-* script hardcodes, so this
    // is where it is unless OMARCHY_PATH says otherwise.
    QStringList roots;
    const QByteArray fromEnv = qgetenv("OMARCHY_PATH");
    if (!fromEnv.isEmpty())
        roots << QString::fromLocal8Bit(fromEnv);
    roots << QDir::homePath() + QStringLiteral("/.local/share/omarchy");

    for (const QString &root : roots) {
        const QString path = root + QStringLiteral("/default/fonts/omarchy/omarchy.ttf");
        if (QFile::exists(path)) {
            qCDebug(lcLayouts) << "icon font:" << path;
            return QUrl::fromLocalFile(path);
        }
    }

    qCInfo(lcLayouts) << "no omarchy icon font found; keys that ask for it will "
                         "use their text fallback";
    return {};
}
