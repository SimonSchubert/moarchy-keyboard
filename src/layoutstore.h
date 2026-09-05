#pragma once

#include <QQmlEngine>
#include <QtGlobal>

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
    QML_NAMED_ELEMENT(Layouts)
    QML_SINGLETON
    Q_PROPERTY(QStringList names READ names NOTIFY changed)
    // Which layout to open on. Lives here rather than as a loose context
    // property so the QML has no unqualified globals left at all.
    Q_PROPERTY(QString initialLayout READ initialLayout CONSTANT)

public:
    // See the note above the class.
    static void setInstance(LayoutStore *instance) { s_instance = instance; }
    static LayoutStore *create(QQmlEngine *, QJSEngine *) {
        // NOT Q_ASSERT: it compiles out under NDEBUG, and the packaged build is
        // Release, so a null instance would be a segfault in the field and a
        // clean abort only on a developer's machine. Returning nullptr makes
        // QML report an unavailable singleton, which is loud and survivable.
        if (!s_instance) {
            qCritical("LayoutStore singleton used before main() set the instance");
            return nullptr;
        }
        // The engine must never delete an object main owns.
        QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
        return s_instance;
    }

    explicit LayoutStore(QObject *parent = nullptr);

    QStringList names() const { return m_names; }

    QString initialLayout() const { return m_initialLayout; }
    void setInitialLayout(const QString &name) { m_initialLayout = name; }

    // The parsed layout, or an empty map with a warning logged. QML asks for
    // these by name; there is no separate id space.
    Q_INVOKABLE QVariantMap layout(const QString &name);

    void reload();

    // Every distinct single-character string any loaded layout can emit,
    // including long-press alternates. VirtualKeyboard compiles a keymap that
    // covers these, so none of them is text-path-only.
    QStringList allCharacters();

Q_SIGNALS:
    void changed();

private:
    QStringList searchPaths() const;

    QString m_initialLayout = QStringLiteral("letters");
    QStringList m_names;
    QHash<QString, QVariantMap> m_cache;

    inline static LayoutStore *s_instance = nullptr;
};
