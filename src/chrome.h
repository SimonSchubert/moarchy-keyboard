#pragma once

#include "qmlsingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>

class QFileSystemWatcher;
class QTimer;

// moarchy's chrome preference, read from ~/.config/omarchy/ui.toml (AC 59).
//
// The shape half of a phone whose colour half is colors.toml: how round the
// shell's sheets, tiles and cards are, chosen with `moarchy-ui corners` or the
// theme picker and independent of the palette. The keyboard reads one number
// out of it -- the *tile* radius -- because the restore handle is a control
// floating over the shell's own, and the shell draws its Pill, Fab and
// IconButton from that number. A handle that stayed a capsule on a square phone
// would be the one control that did not get the message.
//
// The keys do not read it. SPEC.md §6 Q4 is why, and still holds.
//
// A mirror, not a port: the presets, the aliases and the override key below
// are moarchy's default/omarchy/plugins/moarchy.common/Ui.js, which is the
// contract. That file is JavaScript in another repo and cannot be shared, so if
// a preset moves there it has to move here -- `--check-ui` prints what this
// parser makes of a file, so the two can be compared without a phone.
class Chrome : public QObject, public MainOwnedSingleton<Chrome>
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Chrome)
    QML_SINGLETON

    // large | modest | square, after Ui.js's aliases.
    Q_PROPERTY(QString corners READ corners NOTIFY changed)

    // style.md D1's tile radius in logical px: the preset's, or a `tile = N`
    // line's. Uncapped -- a control caps it at half its own short side, which
    // is what keeps `large` a capsule rather than a rounded rectangle.
    Q_PROPERTY(int tile READ tile NOTIFY changed)

public:
    // No default argument, like every singleton main() owns. See
    // MOARCHY_SINGLETON_INVARIANT at the bottom.
    explicit Chrome(QObject *parent);

    // Watches the file and reloads on change. A missing file is `large`, and
    // is picked up when it appears.
    void start(const QString &path);

    // One read, no watch, for --check-ui.
    void loadOnce(const QString &path);

    QString corners() const { return m_corners; }
    int tile() const { return m_tile; }

Q_SIGNALS:
    void changed();

private:
    void reload();
    void rearmWatch();

    QString m_path;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;

    bool m_loaded = false;
    QString m_corners = QStringLiteral("large");
    int m_tile = 20;
};

MOARCHY_SINGLETON_INVARIANT(Chrome);
