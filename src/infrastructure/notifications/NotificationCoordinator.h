#pragma once

#include <QString>

class QSystemTrayIcon;

namespace productivity::infrastructure {

class NotificationCoordinator final
{
public:
    explicit NotificationCoordinator(QSystemTrayIcon* trayIcon);

    bool notify(const QString& title, const QString& message);

private:
    QSystemTrayIcon* trayIcon_ = nullptr;
};

} // namespace productivity::infrastructure
