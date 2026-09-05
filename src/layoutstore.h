#pragma once

#include "qmlsingleton.h"

#include <QQmlEngine>
#include <QtGlobal>

#include <QHash>
#include <QUrl>
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
class LayoutStore : public QObject, public MainOwnedSingleton<LayoutStore>
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Layouts)
    QML_SINGLETON
    Q_PROPERTY(QStringList names READ names NOTIFY changed)
    // Which layout to open on. Lives here rather than as a loose context
    // property so the QML has no unqualified globals left at all.
    Q_PROPERTY(QString initialLayout READ initialLayout CONSTANT)

    // Omarchy's own icon font, which carries its logo at \ue900 -- the same
    // glyph the shell's menu widget uses in the bar. Empty if it is not
    // installed, and the QML falls back to a text label rather than drawing a
    // missing-glyph box.
    Q_PROPERTY(QUrl iconFontUrl READ iconFontUrl CONSTANT)

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
    explicit LayoutStore(QObject *parent);

    QStringList names() const { return m_names; }

    QString initialLayout() const { return m_initialLayout; }

    QUrl iconFontUrl() const;
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

};

// See MainOwnedSingleton in qmlsingleton.h: a default-constructible
// QML_SINGLETON is built by the engine instead of by create(), and QML then
// holds a different object from the one main() configured. That shipped once
// and drew every surface black, so it is a build error instead.
MOARCHY_SINGLETON_INVARIANT(LayoutStore);
