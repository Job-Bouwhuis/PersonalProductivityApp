#include "TimerService.h"

#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <utility>

Q_LOGGING_CATEGORY(timerLog, "application.scheduler")

namespace productivity::services {
namespace {

QString statusName(TimerStatus status)
{
    switch (status) {
    case TimerStatus::Active: return QStringLiteral("active");
    case TimerStatus::Completed: return QStringLiteral("completed");
    case TimerStatus::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("active");
}

QString policyName(NotificationPolicy policy)
{
    switch (policy) {
    case NotificationPolicy::None: return QStringLiteral("none");
    case NotificationPolicy::Deadline: return QStringLiteral("deadline");
    case NotificationPolicy::Recurring: return QStringLiteral("recurring");
    }
    return QStringLiteral("none");
}

TimerStatus parseStatus(const QString& status)
{
    if (status == QLatin1String("completed")) return TimerStatus::Completed;
    if (status == QLatin1String("cancelled")) return TimerStatus::Cancelled;
    return TimerStatus::Active;
}

NotificationPolicy parsePolicy(const QString& policy)
{
    if (policy == QLatin1String("deadline")) return NotificationPolicy::Deadline;
    if (policy == QLatin1String("recurring")) return NotificationPolicy::Recurring;
    return NotificationPolicy::None;
}

bool updateStatus(QSqlDatabase database, const QString& eventId, TimerStatus status)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "UPDATE scheduled_events SET status = :status, updated_at_utc = :updated "
        "WHERE id = :id AND status = 'active'"));
    query.bindValue(QStringLiteral(":status"), statusName(status));
    query.bindValue(QStringLiteral(":updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":id"), eventId);
    if (!query.exec()) {
        qCWarning(timerLog) << "Could not update scheduled event status:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool ensureColumn(QSqlDatabase database, const QString& name, const QString& definition)
{
    QSqlQuery columns(database);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(scheduled_events)")))
        return false;
    while (columns.next()) {
        if (columns.value(1).toString() == name)
            return true;
    }
    QSqlQuery alter(database);
    return alter.exec(QStringLiteral("ALTER TABLE scheduled_events ADD COLUMN ") + name + definition);
}

} // namespace

TimerService::TimerService(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool TimerService::initialize()
{
    if (!database_.isValid() || !database_.isOpen()) {
        qCCritical(timerLog) << "Timer service requires an open database connection";
        return false;
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS scheduled_events ("
            "id TEXT PRIMARY KEY,"
            "title TEXT NOT NULL,"
            "deadline_utc TEXT NOT NULL,"
            "status TEXT NOT NULL CHECK(status IN ('active', 'completed', 'cancelled')),"
            "notification_policy TEXT NOT NULL CHECK(notification_policy IN ('none', 'deadline', 'recurring')),"
            "created_at_utc TEXT NOT NULL,"
            "updated_at_utc TEXT NOT NULL,"
            "paused INTEGER NOT NULL DEFAULT 0,"
            "paused_remaining_ms INTEGER NOT NULL DEFAULT 0)")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS notification_deliveries ("
            "event_id TEXT NOT NULL REFERENCES scheduled_events(id) ON DELETE CASCADE,"
            "notification_key TEXT NOT NULL,"
            "recorded_at_utc TEXT NOT NULL,"
            "PRIMARY KEY(event_id, notification_key))"))) {
        qCCritical(timerLog) << "Could not initialize timer storage:" << query.lastError().text();
        return false;
    }

    if (!ensureColumn(database_, QStringLiteral("paused"), QStringLiteral(" INTEGER NOT NULL DEFAULT 0")) ||
        !ensureColumn(database_, QStringLiteral("paused_remaining_ms"), QStringLiteral(" INTEGER NOT NULL DEFAULT 0"))) {
        qCCritical(timerLog) << "Could not add pause state to timer storage";
        return false;
    }

    const auto recovered = activeEvents();
    qCInfo(timerLog) << "Timer service initialized; recovered active events:" << recovered.size();
    return true;
}

QString TimerService::schedule(QString title, QDateTime deadline, NotificationPolicy policy,
                               QString stableId)
{
    title = title.trimmed();
    if (title.isEmpty() || !deadline.isValid() || !database_.isOpen()) {
        qCWarning(timerLog) << "Rejected invalid scheduled event";
        return {};
    }

    if (stableId.isEmpty())
        stableId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO scheduled_events "
        "(id, title, deadline_utc, status, notification_policy, created_at_utc, updated_at_utc) "
        "VALUES (:id, :title, :deadline, 'active', :policy, :created, :updated)"));
    query.bindValue(QStringLiteral(":id"), stableId);
    query.bindValue(QStringLiteral(":title"), title);
    query.bindValue(QStringLiteral(":deadline"), deadline.toUTC().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":policy"), policyName(policy));
    query.bindValue(QStringLiteral(":created"), now);
    query.bindValue(QStringLiteral(":updated"), now);
    if (!query.exec()) {
        const QSqlError error = query.lastError();
        qCWarning(timerLog) << "Could not schedule event; database error type:"
                            << static_cast<int>(error.type())
                            << "native code:" << error.nativeErrorCode();
        return {};
    }
    return stableId;
}

QList<ScheduledEvent> TimerService::activeEvents() const
{
    QList<ScheduledEvent> events;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, title, deadline_utc, status, notification_policy "
            "FROM scheduled_events WHERE status = 'active' AND paused = 0 ORDER BY deadline_utc"))) {
        qCWarning(timerLog) << "Could not recover scheduled events:" << query.lastError().text();
        return events;
    }

    while (query.next()) {
        ScheduledEvent event;
        event.id = query.value(0).toString();
        event.title = query.value(1).toString();
        event.deadline = QDateTime::fromString(query.value(2).toString(), Qt::ISODate).toUTC();
        event.status = parseStatus(query.value(3).toString());
        event.notificationPolicy = parsePolicy(query.value(4).toString());
        if (event.deadline.isValid())
            events.append(event);
    }
    return events;
}

QList<ScheduledEvent> TimerService::timerEvents() const
{
    QList<ScheduledEvent> events;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, title, deadline_utc, status, notification_policy, paused, paused_remaining_ms "
            "FROM scheduled_events WHERE status = 'active' ORDER BY deadline_utc"))) {
        qCWarning(timerLog) << "Could not load timer overview events:" << query.lastError().text();
        return events;
    }
    while (query.next()) {
        ScheduledEvent event;
        event.id = query.value(0).toString();
        event.title = query.value(1).toString();
        event.deadline = QDateTime::fromString(query.value(2).toString(), Qt::ISODate).toUTC();
        event.status = parseStatus(query.value(3).toString());
        event.notificationPolicy = parsePolicy(query.value(4).toString());
        event.paused = query.value(5).toBool();
        event.remainingMilliseconds = query.value(6).toLongLong();
        if (event.deadline.isValid())
            events.append(event);
    }
    return events;
}

bool TimerService::cancel(const QString& eventId)
{
    return updateStatus(database_, eventId, TimerStatus::Cancelled);
}

bool TimerService::complete(const QString& eventId)
{
    return updateStatus(database_, eventId, TimerStatus::Completed);
}

bool TimerService::pause(const QString& eventId)
{
    if (!database_.transaction())
        return false;
    QSqlQuery lookup(database_);
    lookup.prepare(QStringLiteral(
        "SELECT deadline_utc FROM scheduled_events WHERE id = :id AND status = 'active' AND paused = 0"));
    lookup.bindValue(QStringLiteral(":id"), eventId);
    if (!lookup.exec() || !lookup.next()) {
        database_.rollback();
        return false;
    }
    const QDateTime deadline = QDateTime::fromString(lookup.value(0).toString(), Qt::ISODate).toUTC();
    lookup.finish();
    if (!deadline.isValid()) {
        database_.rollback();
        return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE scheduled_events SET paused = 1, paused_remaining_ms = :remaining, updated_at_utc = :updated "
        "WHERE id = :id AND status = 'active' AND paused = 0"));
    update.bindValue(QStringLiteral(":remaining"), qMax<qint64>(0, now.msecsTo(deadline)));
    update.bindValue(QStringLiteral(":updated"), now.toString(Qt::ISODateWithMs));
    update.bindValue(QStringLiteral(":id"), eventId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        qCWarning(timerLog) << "Could not persist timer pause atomically:" << update.lastError().text();
        database_.rollback();
        return false;
    }
    update.finish();
    if (!database_.commit()) {
        qCWarning(timerLog) << "Could not commit timer pause transaction:" << database_.lastError().text();
        database_.rollback();
        return false;
    }
    return true;
}

bool TimerService::resume(const QString& eventId)
{
    if (!database_.transaction())
        return false;
    QSqlQuery lookup(database_);
    lookup.prepare(QStringLiteral(
        "SELECT paused_remaining_ms FROM scheduled_events WHERE id = :id AND status = 'active' AND paused = 1"));
    lookup.bindValue(QStringLiteral(":id"), eventId);
    if (!lookup.exec() || !lookup.next()) {
        database_.rollback();
        return false;
    }
    const qint64 remaining = qMax<qint64>(0, lookup.value(0).toLongLong());
    lookup.finish();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE scheduled_events SET paused = 0, paused_remaining_ms = 0, deadline_utc = :deadline, "
        "updated_at_utc = :updated WHERE id = :id AND status = 'active' AND paused = 1"));
    update.bindValue(QStringLiteral(":deadline"), now.addMSecs(remaining).toString(Qt::ISODateWithMs));
    update.bindValue(QStringLiteral(":updated"), now.toString(Qt::ISODateWithMs));
    update.bindValue(QStringLiteral(":id"), eventId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        database_.rollback();
        return false;
    }
    update.finish();

    QSqlQuery clearDelivery(database_);
    clearDelivery.prepare(QStringLiteral("DELETE FROM notification_deliveries WHERE event_id = :id"));
    clearDelivery.bindValue(QStringLiteral(":id"), eventId);
    if (!clearDelivery.exec()) {
        qCWarning(timerLog) << "Could not reset notification records after resuming timer:" << clearDelivery.lastError().text();
        database_.rollback();
        return false;
    }
    clearDelivery.finish();
    if (!database_.commit()) {
        qCWarning(timerLog) << "Could not commit timer resume transaction:" << database_.lastError().text();
        database_.rollback();
        return false;
    }
    return true;
}

bool TimerService::reschedule(const QString& eventId, const QDateTime& deadline)
{
    if (eventId.isEmpty() || !deadline.isValid())
        return false;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE scheduled_events SET deadline_utc = :deadline, updated_at_utc = :updated "
        "WHERE id = :id AND status = 'active'"));
    query.bindValue(QStringLiteral(":deadline"), deadline.toUTC().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":id"), eventId);
    if (!query.exec()) {
        qCWarning(timerLog) << "Could not reschedule event:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool TimerService::recordNotification(const QString& eventId, const QString& notificationKey)
{
    if (eventId.isEmpty() || notificationKey.isEmpty())
        return false;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO notification_deliveries (event_id, notification_key, recorded_at_utc) "
        "VALUES (:event, :key, :recorded)"));
    query.bindValue(QStringLiteral(":event"), eventId);
    query.bindValue(QStringLiteral(":key"), notificationKey);
    query.bindValue(QStringLiteral(":recorded"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        qCWarning(timerLog) << "Could not record notification key:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool TimerService::clearNotificationRecord(const QString& eventId, const QString& notificationKey)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "DELETE FROM notification_deliveries WHERE event_id = :event AND notification_key = :key"));
    query.bindValue(QStringLiteral(":event"), eventId);
    query.bindValue(QStringLiteral(":key"), notificationKey);
    if (!query.exec()) {
        qCWarning(timerLog) << "Could not clear notification delivery record:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

} // namespace productivity::services
