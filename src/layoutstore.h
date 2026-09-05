#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

// Layouts are data, not code (AC 18): one JSON file each, looked up in
//
//   1. ~/.config/moarchy-keyboard/layouts/   -- the user's, wins outright (AC 19)
//   2. $PREFIX/share/moarchy-keyboard/layouts/
//   3. :/layouts/                            -- compiled in, so a broken
//                                               install still types
//
// Adding a layout is dropping a file in (1) and restarting. Editing one does
// not need a rebuild.
class LayoutStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList names READ names NOTIFY changed)

public:
    explicit LayoutStore(QObject *parent = nullptr);

    QStringList names() const { return m_names; }

    // The parsed layout, or an empty map with a warning logged. QML asks for
    // these by name; there is no separate id space.
    Q_INVOKABLE QVariantMap layout(const QString &name);

    void reload();

Q_SIGNALS:
    void changed();

private:
    QStringList searchPaths() const;

    QStringList m_names;
    QHash<QString, QVariantMap> m_cache;
};
