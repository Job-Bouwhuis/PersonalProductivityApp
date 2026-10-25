#include "NotificationCoordinator.h"

#include <QLoggingCategory>
#include <QSystemTrayIcon>
#include <QVariantMap>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

Q_LOGGING_CATEGORY(notificationLog, "application.notifications")

namespace productivity::infrastructure {

NotificationCoordinator::NotificationCoordinator(QSystemTrayIcon* trayIcon)
    : trayIcon_(trayIcon)
{
}

bool NotificationCoordinator::notify(const QString& title, const QString& message)
{
    QDBusInterface notifications(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QDBusConnection::sessionBus());

    const QVariantMap hints{
        {QStringLiteral("desktop-entry"), QStringLiteral("personal-productivity-app")}
    };
    const QDBusReply<uint> reply = notifications.call(
        QStringLiteral("Notify"),
        QStringLiteral("Personal Productivity Service"),
        uint(0),
        QStringLiteral("personal-productivity-app"),
        title.toHtmlEscaped(),
        message.toHtmlEscaped(),
        QStringList{},
        hints,
        qint32(10000));
    if (reply.isValid()) {
        qCInfo(notificationLog) << "Desktop notification sent";
        return true;
    }
    qCWarning(notificationLog) << "Desktop notification service call failed:" << reply.error().message();

    if (trayIcon_ && trayIcon_->isVisible()) {
        trayIcon_->showMessage(title, message, QSystemTrayIcon::Information, 10000);
        qCInfo(notificationLog) << "Displayed timer notification through the system tray";
        return true;
    }
    return false;
}

} // namespace productivity::infrastructure
