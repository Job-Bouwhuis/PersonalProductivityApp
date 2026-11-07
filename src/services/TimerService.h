#pragma once

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace productivity::services {

enum class TimerStatus {
    Active,
    Completed,
    Cancelled,
};

enum class NotificationPolicy {
    None,
    Deadline,
    Recurring,
};

struct ScheduledEvent {
    QString id;
    QString title;
    QDateTime deadline;
    QDateTime createdAt;
    TimerStatus status = TimerStatus::Active;
    NotificationPolicy notificationPolicy = NotificationPolicy::None;
    bool paused = false;
    qint64 remainingMilliseconds = 0;
    qint64 recurrenceIntervalMilliseconds = 0;
};

class TimerService final
{
public:
    explicit TimerService(QSqlDatabase database);

    bool initialize();
    static QDateTime nextChristmasDeadline(const QDateTime& now = QDateTime::currentDateTime());
    QString schedule(QString title, QDateTime deadline, NotificationPolicy policy,
                     QString stableId = {}, bool useMonotonicClock = false,
                     qint64 recurrenceIntervalMilliseconds = 0);
    QList<ScheduledEvent> activeEvents() const;
    QList<ScheduledEvent> timerEvents() const;
    bool cancel(const QString& eventId);
    bool complete(const QString& eventId);
    bool pause(const QString& eventId);
    bool resume(const QString& eventId);
    bool reschedule(const QString& eventId, const QDateTime& deadline);

    // Returns true only the first time a notification key is recorded for an event.
    bool recordNotification(const QString& eventId, const QString& notificationKey);
    bool clearNotificationRecord(const QString& eventId, const QString& notificationKey);

private:
    QSqlDatabase database_;
};

} // namespace productivity::services
