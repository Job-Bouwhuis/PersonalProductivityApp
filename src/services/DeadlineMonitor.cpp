#include "DeadlineMonitor.h"

#include "TimerService.h"
#include "infrastructure/notifications/NotificationCoordinator.h"

#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(deadlineMonitorLog, "application.scheduler")

namespace productivity::services {

DeadlineMonitor::DeadlineMonitor(TimerService& timerService,
                                 infrastructure::NotificationCoordinator& notificationCoordinator,
                                 QObject* parent)
    : QObject(parent)
    , timerService_(timerService)
    , notificationCoordinator_(notificationCoordinator)
{
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, [this] { checkDueEvents(); });
}

void DeadlineMonitor::start()
{
    checkDueEvents();
    timer_.start();
}

void DeadlineMonitor::restart()
{
    timer_.stop();
    retryAfter_.clear();
    timer_.start();
}

void DeadlineMonitor::stop()
{
    timer_.stop();
    retryAfter_.clear();
}

void DeadlineMonitor::checkDueEvents()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto events = timerService_.activeEvents();
    for (const auto& event : events) {
        if (event.notificationPolicy != NotificationPolicy::Deadline || event.deadline > now)
            continue;
        if (retryAfter_.value(event.id) > now)
            continue;

        const QString notificationKey = QStringLiteral("deadline");
        if (!timerService_.recordNotification(event.id, notificationKey))
            continue;

        if (!notificationCoordinator_.notify(QStringLiteral("Timer due"), event.title)) {
            timerService_.clearNotificationRecord(event.id, notificationKey);
            retryAfter_.insert(event.id, now.addSecs(30));
            qCWarning(deadlineMonitorLog) << "Could not deliver due notification for event" << event.id;
        } else {
            retryAfter_.remove(event.id);
        }
    }
}

} // namespace productivity::services
