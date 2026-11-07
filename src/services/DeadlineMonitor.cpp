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
    finalCountdownSecond_.clear();
    timer_.start();
}

void DeadlineMonitor::stop()
{
    timer_.stop();
    retryAfter_.clear();
    finalCountdownSecond_.clear();
}

void DeadlineMonitor::checkDueEvents()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto events = timerService_.activeEvents();
    for (const auto& event : events) {
        if (event.notificationPolicy == NotificationPolicy::Recurring) {
            if (event.deadline > now || retryAfter_.value(event.id) > now)
                continue;
            if (event.recurrenceIntervalMilliseconds <= 0 && event.recurrenceIntervalMilliseconds != -1) {
                qCWarning(deadlineMonitorLog) << "Recurring event has no valid interval; cancelling" << event.id;
                timerService_.cancel(event.id);
                continue;
            }
            const QString notificationKey = QStringLiteral("recurring-due");
            const bool christmasTimer = event.id == QLatin1String("christmas-timer");
            const auto nextOccurrence = [&event, &now, christmasTimer] {
                return christmasTimer
                    ? TimerService::nextChristmasDeadline(now.toLocalTime()).toUTC()
                    : now.addMSecs(event.recurrenceIntervalMilliseconds);
            };
            if (!timerService_.recordNotification(event.id, notificationKey)) {
                const QString nextId = timerService_.schedule(
                    event.title, nextOccurrence(),
                    NotificationPolicy::Recurring, event.id, !christmasTimer, event.recurrenceIntervalMilliseconds);
                if (nextId.isEmpty())
                    qCWarning(deadlineMonitorLog) << "Could not advance recurring event after a recorded delivery" << event.id;
                continue;
            }
            const QString notificationTitle = christmasTimer ? QStringLiteral("Christmas countdown")
                                                              : QStringLiteral("Drink reminder");
            const QString notificationText = christmasTimer ? QStringLiteral("Merry Christmas! 🎄") : event.title;
            if (!notificationCoordinator_.notify(notificationTitle, notificationText, {},
                                                  QStringLiteral("recurring:%1").arg(event.id))) {
                timerService_.clearNotificationRecord(event.id, notificationKey);
                retryAfter_.insert(event.id, now.addSecs(30));
                qCWarning(deadlineMonitorLog) << "Could not deliver recurring reminder for event" << event.id;
                continue;
            }
            retryAfter_.remove(event.id);
            const QString nextId = timerService_.schedule(
                event.title, nextOccurrence(),
                NotificationPolicy::Recurring, event.id, !christmasTimer, event.recurrenceIntervalMilliseconds);
            if (nextId.isEmpty())
                qCWarning(deadlineMonitorLog) << "Could not schedule the next recurring reminder" << event.id;
            continue;
        }
        if (event.notificationPolicy != NotificationPolicy::Deadline)
            continue;
        if (retryAfter_.value(event.id) > now)
            continue;

        if (event.deadline > now) {
            const QDateTime localNow = now.toLocalTime();
            const QDateTime localDeadline = event.deadline.toLocalTime();
            const qint64 remainingMs = now.msecsTo(event.deadline);
            constexpr qint64 earlyNoticeMs = 10 * 1000;

            if (remainingMs <= earlyNoticeMs) {
                const QList<int> milestonesMinutes{30, 15, 5};
                for (const int minutes : milestonesMinutes) {
                    const qint64 milestoneMs = static_cast<qint64>(minutes) * 60 * 1000;
                    if (remainingMs <= milestoneMs + earlyNoticeMs && event.createdAt.isValid() &&
                        event.createdAt <= event.deadline.addMSecs(-milestoneMs - earlyNoticeMs)) {
                        timerService_.recordNotification(event.id, QStringLiteral("final-hour-%1m").arg(minutes));
                    }
                }
                const QString countdownKey = QStringLiteral("final-countdown");
                const bool firstCountdownUpdate = timerService_.recordNotification(event.id, countdownKey);
                const int secondsRemaining = static_cast<int>((remainingMs + 999) / 1000);
                if (firstCountdownUpdate || finalCountdownSecond_.value(event.id, -1) != secondsRemaining) {
                    const bool delivered = notificationCoordinator_.notify(
                        QStringLiteral("Deadline countdown"),
                        QStringLiteral("%1 seconds remaining: %2").arg(secondsRemaining).arg(event.title),
                        {}, QStringLiteral("deadline:%1:final-countdown").arg(event.id), 20000);
                    if (delivered) {
                        finalCountdownSecond_.insert(event.id, secondsRemaining);
                        retryAfter_.remove(event.id);
                    } else {
                        timerService_.clearNotificationRecord(event.id, countdownKey);
                        retryAfter_.insert(event.id, now.addSecs(1));
                        qCWarning(deadlineMonitorLog) << "Could not deliver final countdown notification for event" << event.id;
                    }
                }
                continue;
            }

            if (remainingMs <= 60 * 60 * 1000) {
                const QList<int> milestonesMinutes{30, 15, 5};
                int nearestMissedMilestone = 0;
                for (const int minutes : milestonesMinutes) {
                    const qint64 milestoneMs = static_cast<qint64>(minutes) * 60 * 1000;
                    const QDateTime notifyAt = event.deadline.addMSecs(-milestoneMs - earlyNoticeMs);
                    if (remainingMs <= milestoneMs + earlyNoticeMs &&
                        event.createdAt.isValid() && event.createdAt <= notifyAt)
                        nearestMissedMilestone = minutes;
                }
                if (nearestMissedMilestone != 0) {
                    const QString key = QStringLiteral("final-hour-%1m").arg(nearestMissedMilestone);
                    const bool shouldNotify = timerService_.recordNotification(event.id, key);
                    for (const int minutes : milestonesMinutes) {
                        if (minutes > nearestMissedMilestone &&
                            remainingMs <= static_cast<qint64>(minutes) * 60 * 1000 + earlyNoticeMs) {
                            timerService_.recordNotification(event.id, QStringLiteral("final-hour-%1m").arg(minutes));
                        }
                    }
                    if (shouldNotify && !notificationCoordinator_.notify(
                            QStringLiteral("Deadline approaching"),
                            QStringLiteral("%1 minutes remaining: %2").arg(nearestMissedMilestone).arg(event.title),
                            {}, QStringLiteral("deadline:%1:milestone").arg(event.id), 20000)) {
                        timerService_.clearNotificationRecord(event.id, key);
                        retryAfter_.insert(event.id, now.addSecs(30));
                        qCWarning(deadlineMonitorLog) << "Could not deliver final-hour milestone for event" << event.id;
                    }
                }
                continue;
            }

            if (localNow.date() == localDeadline.date()) {
                const QList<int> milestonesHours{18, 12, 6, 3};
                int nearestMissedMilestone = 0;
                for (const int hours : milestonesHours) {
                    const qint64 milestoneMs = static_cast<qint64>(hours) * 60 * 60 * 1000;
                    const QDateTime notifyAt = event.deadline.addMSecs(-milestoneMs - earlyNoticeMs);
                    if (remainingMs <= milestoneMs + earlyNoticeMs &&
                        event.createdAt.isValid() && event.createdAt <= notifyAt)
                        nearestMissedMilestone = hours;
                }
                if (nearestMissedMilestone != 0) {
                    const QString key = QStringLiteral("same-day-%1h").arg(nearestMissedMilestone);
                    const bool shouldNotify = timerService_.recordNotification(event.id, key);
                    for (const int hours : milestonesHours) {
                        if (hours > nearestMissedMilestone &&
                            remainingMs <= static_cast<qint64>(hours) * 60 * 60 * 1000 + earlyNoticeMs) {
                            timerService_.recordNotification(event.id, QStringLiteral("same-day-%1h").arg(hours));
                        }
                    }
                    if (shouldNotify && !notificationCoordinator_.notify(
                            QStringLiteral("Deadline approaching"),
                            QStringLiteral("%1 hours remaining: %2").arg(nearestMissedMilestone).arg(event.title),
                            {}, QStringLiteral("deadline:%1:same-day").arg(event.id), 20000)) {
                        timerService_.clearNotificationRecord(event.id, key);
                        retryAfter_.insert(event.id, now.addSecs(30));
                        qCWarning(deadlineMonitorLog) << "Could not deliver upcoming deadline notification for event" << event.id;
                    }
                }
            }
            continue;
        }

        const QString notificationKey = QStringLiteral("deadline");
        if (!timerService_.recordNotification(event.id, notificationKey))
            continue;

        if (!notificationCoordinator_.notify(QStringLiteral("Deadline reached"),
                                              QStringLiteral("Due now: %1").arg(event.title), {},
                                              QStringLiteral("deadline:%1:final-countdown").arg(event.id), 20000)) {
            timerService_.clearNotificationRecord(event.id, notificationKey);
            retryAfter_.insert(event.id, now.addSecs(30));
            qCWarning(deadlineMonitorLog) << "Could not deliver due notification for event" << event.id;
        } else {
            retryAfter_.remove(event.id);
            finalCountdownSecond_.remove(event.id);
            if (event.id.startsWith(QStringLiteral("todo:")) && !timerService_.complete(event.id))
                qCWarning(deadlineMonitorLog) << "Deadline notification was delivered, but the TODO timer event could not be completed" << event.id;
        }
    }
}

} // namespace productivity::services
