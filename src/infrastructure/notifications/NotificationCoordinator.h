#pragma once

#include <QString>

class QSystemTrayIcon;
namespace productivity::services { class WorkDescriptorService; }

namespace productivity::infrastructure {

class NotificationCoordinator final
{
public:
    NotificationCoordinator(QSystemTrayIcon* trayIcon,
                            productivity::services::WorkDescriptorService& workService);

    bool notify(const QString& title, const QString& message, const QString& parentWorkId = {});

private:
    QSystemTrayIcon* trayIcon_ = nullptr;
    productivity::services::WorkDescriptorService& workService_;
};

} // namespace productivity::infrastructure
