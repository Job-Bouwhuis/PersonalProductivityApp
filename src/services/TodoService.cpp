#include "TodoService.h"

#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QFileInfo>
#include <QUuid>

#include <utility>

Q_LOGGING_CATEGORY(todoLog, "application.todos")

namespace productivity::services {
namespace {

QString statusName(TodoStatus status)
{
    return status == TodoStatus::Completed ? QStringLiteral("completed") : QStringLiteral("open");
}

QList<TodoRecord> readTodos(QSqlQuery& query)
{
    QList<TodoRecord> items;
    while (query.next()) {
        TodoRecord item;
        item.id = query.value(0).toString();
        item.title = query.value(1).toString();
        item.description = query.value(2).toString();
        item.status = query.value(3).toString() == QLatin1String("completed")
            ? TodoStatus::Completed : TodoStatus::Open;
        item.deadline = QDateTime::fromString(query.value(4).toString(), Qt::ISODate).toUTC();
        item.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate).toUTC();
        item.linkType = query.value(6).toString();
        item.linkTarget = query.value(7).toString();
        item.linkLabel = query.value(8).toString();
        items.append(item);
    }
    return items;
}

bool validLink(const TodoRecord& todo, QSqlDatabase database)
{
    if (todo.linkType.isEmpty())
        return todo.linkTarget.isEmpty();
    if (todo.linkTarget.isEmpty())
        return false;
    if (todo.linkType == QLatin1String("file"))
        return QFileInfo(todo.linkTarget).isAbsolute();
    if (todo.linkType == QLatin1String("website")) {
        const QUrl url(todo.linkTarget, QUrl::StrictMode);
        return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
               (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
    }
    if (todo.linkType == QLatin1String("note")) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT 1 FROM persistent_notes WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), todo.linkTarget);
        return query.exec() && query.next();
    }
    if (todo.linkType == QLatin1String("quick_note")) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT 1 FROM quick_notes WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), todo.linkTarget);
        return query.exec() && query.next();
    }
    return false;
}

} // namespace

TodoService::TodoService(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool TodoService::initialize()
{
    QSqlQuery query(database_);
    if (!database_.isOpen() || !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS todos ("
            "id TEXT PRIMARY KEY,"
            "title TEXT NOT NULL,"
            "description TEXT NOT NULL DEFAULT '',"
            "status TEXT NOT NULL CHECK(status IN ('open', 'completed')) DEFAULT 'open',"
            "deadline_utc TEXT NOT NULL DEFAULT '',"
            "created_at_utc TEXT NOT NULL,"
            "updated_at_utc TEXT NOT NULL,"
            "link_type TEXT NOT NULL DEFAULT '',"
            "link_target TEXT NOT NULL DEFAULT '',"
            "link_label TEXT NOT NULL DEFAULT '')"))) {
        qCCritical(todoLog) << "Could not initialize TODO storage:" << query.lastError().text();
        return false;
    }
    qCInfo(todoLog) << "TODO storage initialized";
    return true;
}

QList<TodoRecord> TodoService::todos(TodoFilter filter, const QString& searchText) const
{
    QString sql = QStringLiteral(
        "SELECT id, title, description, status, deadline_utc, created_at_utc, "
        "link_type, link_target, link_label FROM todos WHERE 1 = 1");
    switch (filter) {
    case TodoFilter::Upcoming:
        sql += QStringLiteral(" AND status = 'open' AND deadline_utc >= :now");
        break;
    case TodoFilter::Overdue:
        sql += QStringLiteral(" AND status = 'open' AND deadline_utc <> '' AND deadline_utc < :now");
        break;
    case TodoFilter::Completed:
        sql += QStringLiteral(" AND status = 'completed'");
        break;
    case TodoFilter::Unscheduled:
        sql += QStringLiteral(" AND status = 'open' AND deadline_utc = ''");
        break;
    case TodoFilter::All:
        break;
    }
    if (!searchText.trimmed().isEmpty())
        sql += QStringLiteral(" AND (title LIKE :search OR description LIKE :search)");
    sql += QStringLiteral(
        " ORDER BY CASE WHEN status = 'completed' THEN 1 ELSE 0 END, "
        "CASE WHEN deadline_utc = '' THEN 1 ELSE 0 END, deadline_utc, created_at_utc DESC");

    QSqlQuery query(database_);
    query.prepare(sql);
    if (filter == TodoFilter::Upcoming || filter == TodoFilter::Overdue)
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!searchText.trimmed().isEmpty())
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%") + searchText.trimmed() + QStringLiteral("%"));
    if (!query.exec()) {
        qCWarning(todoLog) << "Could not load TODO items:" << query.lastError().text();
        return {};
    }
    return readTodos(query);
}

QString TodoService::saveTodo(const TodoRecord& todo)
{
    const QString title = todo.title.trimmed();
    if (!database_.isOpen() || title.isEmpty()) {
        qCWarning(todoLog) << "Rejected invalid TODO item";
        return {};
    }

    if (!database_.transaction()) {
        qCWarning(todoLog) << "Could not begin TODO save transaction:" << database_.lastError().text();
        return {};
    }
    if (!validLink(todo, database_)) {
        qCWarning(todoLog) << "Rejected invalid TODO link";
        database_.rollback();
        return {};
    }

    const QString id = todo.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : todo.id;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString deadline = todo.deadline.isValid()
        ? todo.deadline.toUTC().toString(Qt::ISODateWithMs) : QString();
    QSqlQuery query(database_);
    if (todo.id.isEmpty()) {
        query.prepare(QStringLiteral(
            "INSERT INTO todos (id, title, description, status, deadline_utc, created_at_utc, updated_at_utc, "
            "link_type, link_target, link_label) VALUES "
            "(:id, :title, :description, :status, :deadline, :created, :updated, :link_type, :link_target, :link_label)"));
        query.bindValue(QStringLiteral(":created"), now);
    } else {
        query.prepare(QStringLiteral(
            "UPDATE todos SET title = :title, description = :description, status = :status, "
            "deadline_utc = :deadline, updated_at_utc = :updated, link_type = :link_type, "
            "link_target = :link_target, link_label = :link_label WHERE id = :id"));
    }
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":title"), title);
    query.bindValue(QStringLiteral(":description"), todo.description);
    query.bindValue(QStringLiteral(":status"), statusName(todo.status));
    query.bindValue(QStringLiteral(":deadline"), deadline);
    query.bindValue(QStringLiteral(":updated"), now);
    query.bindValue(QStringLiteral(":link_type"), todo.linkType);
    query.bindValue(QStringLiteral(":link_target"), todo.linkTarget);
    query.bindValue(QStringLiteral(":link_label"), todo.linkLabel);
    if (!query.exec() || query.numRowsAffected() != 1) {
        qCWarning(todoLog) << "Could not save TODO item:" << query.lastError().text();
        database_.rollback();
        return {};
    }
    query.finish();
    if (!database_.commit()) {
        qCWarning(todoLog) << "Could not commit TODO save transaction:" << database_.lastError().text();
        database_.rollback();
        return {};
    }
    return id;
}

bool TodoService::deleteTodo(const QString& id)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM todos WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        qCWarning(todoLog) << "Could not delete TODO item:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

} // namespace productivity::services
