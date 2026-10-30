#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

namespace productivity::services {

struct WorkDescriptor
{
    enum class State { Running, Completed, Failed };

    QString id;
    QString parentId;
    QString title;
    QString detail;
    State state = State::Running;
    std::optional<int> progress;
    QDateTime startedAt;
    QDateTime finishedAt;
};

class WorkDescriptorService final : public QObject
{
    Q_OBJECT
public:
    explicit WorkDescriptorService(QObject* parent = nullptr);

    QString startTask(const QString& title, const QString& parentId = {});
    void updateTask(const QString& id, const QString& detail,
                    std::optional<int> progress = std::nullopt);
    void updateTaskDetail(const QString& id, const QString& detail);
    void completeTask(const QString& id, const QString& detail = QStringLiteral("Completed"));
    void failTask(const QString& id, const QString& detail);

    QVector<WorkDescriptor> tasks() const;
    std::optional<int> progressForTask(const QString& id) const;

signals:
    void tasksChanged();

private:
    std::optional<int> progressForTask(const QString& id, QSet<QString>& visiting) const;
    void finishTask(const QString& id, WorkDescriptor::State state, const QString& detail);
    void pruneFinishedTasks();

    QHash<QString, WorkDescriptor> tasks_;
    QTimer pruneTimer_;
};

} // namespace productivity::services
