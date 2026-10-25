#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace productivity::infrastructure {
class NotificationCoordinator;
}

namespace productivity::services {

class TimerService;

class DeadlineMonitor final : public QObject
{
public:
    DeadlineMonitor(TimerService& timerService,
                    infrastructure::NotificationCoordinator& notificationCoordinator,
                    QObject* parent = nullptr);

    void start();
    void restart();
    void stop();

private:
    void checkDueEvents();

    TimerService& timerService_;
    infrastructure::NotificationCoordinator& notificationCoordinator_;
    QTimer timer_;
    QHash<QString, QDateTime> retryAfter_;
};

} // namespace productivity::services
