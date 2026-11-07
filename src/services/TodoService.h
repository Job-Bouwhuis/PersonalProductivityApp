#pragma once

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace productivity::services {

class TimerService;

enum class TodoStatus {
    Open,
    Completed,
};

enum class TodoFilter {
    All,
    Upcoming,
    Overdue,
    Completed,
    Unscheduled,
};

struct TodoRecord {
    QString id;
    QString title;
    QString description;
    TodoStatus status = TodoStatus::Open;
    QDateTime deadline;
    QDateTime createdAt;
    QString linkType;
    QString linkTarget;
    QString linkLabel;
};

class TodoService final
{
public:
    explicit TodoService(QSqlDatabase database, TimerService* timerService = nullptr);

    bool initialize();
    QList<TodoRecord> todos(TodoFilter filter = TodoFilter::All,
                            const QString& searchText = {}) const;
    QString saveTodo(const TodoRecord& todo);
    bool hasEmailLinkedTodo(const QString& messageId) const;
    bool deleteTodo(const QString& id);

private:
    QSqlDatabase database_;
    TimerService* timerService_ = nullptr;
};

} // namespace productivity::services
