#pragma once

#include <QString>
#include <QHash>

class QSystemTrayIcon;
namespace productivity::services { class WorkDescriptorService; }
namespace productivity::services { class ApplicationSettings; }

namespace productivity::infrastructure {

class NotificationCoordinator final
{
public:
    NotificationCoordinator(QSystemTrayIcon* trayIcon,
                            productivity::services::WorkDescriptorService& workService,
                            productivity::services::ApplicationSettings& settings);

    bool notify(const QString& title, const QString& message, const QString& parentWorkId = {},
                const QString& replacementKey = {}, int expireTimeoutMs = 10000);

private:
    QSystemTrayIcon* trayIcon_ = nullptr;
    productivity::services::WorkDescriptorService& workService_;
    productivity::services::ApplicationSettings& settings_;
    QHash<QString, uint> notificationIds_;
};

} // namespace productivity::infrastructure
