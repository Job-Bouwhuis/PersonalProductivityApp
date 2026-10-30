#include "NotificationCoordinator.h"
#include "WorkDescriptorService.h"

#include <QLoggingCategory>
#include <QSystemTrayIcon>
#include <QVariantMap>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

Q_LOGGING_CATEGORY(notificationLog, "application.notifications")

namespace productivity::infrastructure {

NotificationCoordinator::NotificationCoordinator(QSystemTrayIcon* trayIcon,
                                                   productivity::services::WorkDescriptorService& workService)
    : trayIcon_(trayIcon)
    , workService_(workService)
{
}

bool NotificationCoordinator::notify(const QString& title, const QString& message,
                                     const QString& parentWorkId)
{
    const QString taskId = workService_.startTask(QStringLiteral("Deliver notification"), parentWorkId);
    workService_.updateTask(taskId, title);
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
        workService_.completeTask(taskId, QStringLiteral("Notification delivered"));
        return true;
    }
    qCWarning(notificationLog) << "Desktop notification service call failed:" << reply.error().message();

    if (trayIcon_ && trayIcon_->isVisible()) {
        trayIcon_->showMessage(title, message, QSystemTrayIcon::Information, 10000);
        qCInfo(notificationLog) << "Displayed timer notification through the system tray";
        workService_.completeTask(taskId, QStringLiteral("Notification displayed in the system tray"));
        return true;
    }
    workService_.failTask(taskId, QStringLiteral("No desktop notification service is available"));
    return false;
}

} // namespace productivity::infrastructure
