#include "NotificationCoordinator.h"
#include "ApplicationSettings.h"
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
                                                   productivity::services::WorkDescriptorService& workService,
                                                   productivity::services::ApplicationSettings& settings)
    : trayIcon_(trayIcon)
    , workService_(workService)
    , settings_(settings)
{
}

bool NotificationCoordinator::notify(const QString& title, const QString& message,
                                     const QString& parentWorkId, const QString& replacementKey,
                                     int expireTimeoutMs)
{
    const QString taskId = workService_.startTask(QStringLiteral("Deliver notification"), parentWorkId);
    workService_.updateTask(taskId, title);
    if (!settings_.desktopNotificationsEnabled()) {
        workService_.completeTask(taskId, QStringLiteral("Notification suppressed by Settings"));
        return true;
    }
    QDBusInterface notifications(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QDBusConnection::sessionBus());

    const QVariantMap hints{
        {QStringLiteral("desktop-entry"), QStringLiteral("personal-productivity-app")}
    };
    const uint replacesId = replacementKey.isEmpty() ? 0 : notificationIds_.value(replacementKey, 0);
    QDBusReply<uint> reply = notifications.call(
        QStringLiteral("Notify"),
        QStringLiteral("Personal Productivity Service"),
        replacesId,
        QStringLiteral("personal-productivity-app"),
        title.toHtmlEscaped(),
        message.toHtmlEscaped(),
        QStringList{},
        hints,
        qint32(expireTimeoutMs));
    if (!reply.isValid() && replacesId != 0) {
        qCWarning(notificationLog) << "Notification replacement failed; retrying as a new notification:" << reply.error().message();
        reply = notifications.call(
            QStringLiteral("Notify"),
            QStringLiteral("Personal Productivity Service"),
            uint(0),
            QStringLiteral("personal-productivity-app"),
            title.toHtmlEscaped(),
            message.toHtmlEscaped(),
            QStringList{},
            hints,
            qint32(expireTimeoutMs));
    }
    if (reply.isValid()) {
        if (!replacementKey.isEmpty())
            notificationIds_.insert(replacementKey, reply.value());
        qCInfo(notificationLog) << "Desktop notification sent";
        workService_.completeTask(taskId, QStringLiteral("Notification delivered"));
        return true;
    }
    qCWarning(notificationLog) << "Desktop notification service call failed:" << reply.error().message();

    if (trayIcon_ && trayIcon_->isVisible()) {
        trayIcon_->showMessage(title, message, QSystemTrayIcon::Information, expireTimeoutMs);
        qCInfo(notificationLog) << "Displayed timer notification through the system tray";
        workService_.completeTask(taskId, QStringLiteral("Notification displayed in the system tray"));
        return true;
    }
    workService_.failTask(taskId, QStringLiteral("No desktop notification service is available"));
    return false;
}

} // namespace productivity::infrastructure
