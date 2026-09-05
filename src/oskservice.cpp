#include "oskservice.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLoggingCategory>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcBus, "moarchy.bus")

namespace {
constexpr auto kService = "sm.puri.OSK0";
constexpr auto kPath = "/sm/puri/OSK0";
constexpr auto kInterface = "sm.puri.OSK0";
} // namespace

OskService::OskService(QObject *parent)
    : QObject(parent)
{
}

bool OskService::registerOnBus(QString *error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        *error = QStringLiteral("no session bus -- sm.puri.OSK0 will not be available, "
                                "so mobileomarchy-toggle-keyboard cannot drive this keyboard");
        return false;
    }

    if (!bus.registerObject(QLatin1String(kPath), this,
                            QDBusConnection::ExportAllSlots
                                | QDBusConnection::ExportAllProperties
                                | QDBusConnection::ExportAllSignals)) {
        *error = QStringLiteral("could not export %1").arg(QLatin1String(kPath));
        return false;
    }

    if (!bus.registerService(QLatin1String(kService))) {
        *error = QStringLiteral("could not take the name %1 -- another on-screen "
                                "keyboard is already running")
                     .arg(QLatin1String(kService));
        return false;
    }

    qCInfo(lcBus) << "owning" << kService << "at" << kPath;
    return true;
}

void OskService::SetVisible(bool visible)
{
    setVisible(visible);
}

void OskService::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    emitPropertiesChanged();
    Q_EMIT visibleChanged();
}

void OskService::emitPropertiesChanged()
{
    // QtDBus exports the property for reads but does not emit
    // org.freedesktop.DBus.Properties.PropertiesChanged on its own, and a
    // watcher that trusted the signal would never see the keyboard move.
    QDBusMessage signal = QDBusMessage::createSignal(
        QLatin1String(kPath),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));

    QVariantMap changed;
    changed.insert(QStringLiteral("Visible"), m_visible);

    signal << QLatin1String(kInterface) << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}
