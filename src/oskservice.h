#pragma once

#include <QObject>

// sm.puri.OSK0 -- the interface Phosh defines and squeekboard implements.
//
// Implemented here so mobileomarchy's bin/mobileomarchy-toggle-keyboard keeps
// working with no edit at all (AC 9). That script asks for the `Visible`
// property before it acts rather than blind-toggling, so the property has to be
// genuinely readable, not just present.
class OskService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "sm.puri.OSK0")
    Q_PROPERTY(bool Visible READ visible WRITE setVisible NOTIFY visibleChanged)

public:
    explicit OskService(QObject *parent = nullptr);

    // Returns false if the name is already taken -- which means another OSK is
    // running, and two keyboards on one seat is worse than none.
    bool registerOnBus(QString *error);

    bool visible() const { return m_visible; }
    void setVisible(bool visible);

public Q_SLOTS:
    Q_SCRIPTABLE void SetVisible(bool visible);

Q_SIGNALS:
    void visibleChanged();

private:
    void emitPropertiesChanged();

    bool m_visible = false;
};
