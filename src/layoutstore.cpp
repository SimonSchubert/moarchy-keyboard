#include "layoutstore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSet>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcLayouts, "moarchy.layouts")

LayoutStore::LayoutStore(QObject *parent)
    : QObject(parent)
{
    reload();
}

QStringList LayoutStore::searchPaths() const
{
    return {
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
            + QStringLiteral("/moarchy-keyboard/layouts"),
        QStringLiteral(MOARCHY_DATADIR "/moarchy-keyboard/layouts"),
        QStringLiteral(":/layouts"),
    };
}

void LayoutStore::reload()
{
    m_cache.clear();
    m_names.clear();

    QStringList seen;
    for (const QString &directory : searchPaths()) {
        QDir dir(directory);
        if (!dir.exists())
            continue;
        for (const QFileInfo &info : dir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files)) {
            const QString name = info.completeBaseName();
            // First path wins, so a user layout shadows the shipped one of the
            // same name rather than merging with it.
            if (seen.contains(name))
                continue;
            seen.append(name);
        }
    }

    m_names = seen;
    m_names.sort();
    Q_EMIT changed();
}

QVariantMap LayoutStore::layout(const QString &name)
{
    const auto cached = m_cache.constFind(name);
    if (cached != m_cache.constEnd())
        return *cached;

    for (const QString &directory : searchPaths()) {
        const QString path = directory + QLatin1Char('/') + name + QStringLiteral(".json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QJsonParseError parseError {};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            // Do not fall through to the next path on a parse error: a user
            // layout that is broken should say so, not silently resurrect the
            // shipped one and leave the edit looking like it did nothing.
            qCWarning(lcLayouts) << path << "is not valid JSON:" << parseError.errorString()
                                 << "at offset" << parseError.offset;
            return {};
        }

        const QVariantMap parsed = document.object().toVariantMap();
        m_cache.insert(name, parsed);
        qCDebug(lcLayouts) << "loaded" << name << "from" << path;
        return parsed;
    }

    qCWarning(lcLayouts) << "no layout named" << name << "on any search path";
    return {};
}

QStringList LayoutStore::allCharacters()
{
    QSet<QString> found;

    for (const QString &name : m_names) {
        const QVariantMap parsed = layout(name);
        for (const QVariant &rowValue : parsed.value(QStringLiteral("rows")).toList()) {
            const QVariantMap row = rowValue.toMap();
            for (const QVariant &keyValue : row.value(QStringLiteral("keys")).toList()) {
                const QVariantMap key = keyValue.toMap();

                const QString text = key.value(QStringLiteral("text")).toString();
                if (text.size() == 1)
                    found.insert(text);

                for (const QVariant &alt : key.value(QStringLiteral("alt")).toList()) {
                    const QString value = alt.toString();
                    if (value.size() == 1)
                        found.insert(value);
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
