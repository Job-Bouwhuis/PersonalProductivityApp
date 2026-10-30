#include "WorkDescriptorService.h"

#include <QUuid>

#include <algorithm>

namespace productivity::services {

WorkDescriptorService::WorkDescriptorService(QObject* parent)
    : QObject(parent)
{
    pruneTimer_.setInterval(2000);
    connect(&pruneTimer_, &QTimer::timeout, this, &WorkDescriptorService::pruneFinishedTasks);
    pruneTimer_.start();
}

QString WorkDescriptorService::startTask(const QString& title, const QString& parentId)
{
    WorkDescriptor task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.parentId = tasks_.contains(parentId) ? parentId : QString();
    task.title = title.trimmed().isEmpty() ? QStringLiteral("Background work") : title.trimmed();
    task.startedAt = QDateTime::currentDateTimeUtc();
    tasks_.insert(task.id, task);
    emit tasksChanged();
    return task.id;
}

void WorkDescriptorService::updateTask(const QString& id, const QString& detail,
                                       std::optional<int> progress)
{
    auto it = tasks_.find(id);
    if (it == tasks_.end() || it->state != WorkDescriptor::State::Running)
        return;
    it->detail = detail;
    it->progress = progress.has_value() ? std::optional<int>(qBound(0, *progress, 100)) : std::nullopt;
    emit tasksChanged();
}

void WorkDescriptorService::updateTaskDetail(const QString& id, const QString& detail)
{
    auto it = tasks_.find(id);
    if (it == tasks_.end() || it->state != WorkDescriptor::State::Running)
        return;
    it->detail = detail;
    emit tasksChanged();
}

void WorkDescriptorService::completeTask(const QString& id, const QString& detail)
{
    finishTask(id, WorkDescriptor::State::Completed, detail);
}

void WorkDescriptorService::failTask(const QString& id, const QString& detail)
{
    finishTask(id, WorkDescriptor::State::Failed, detail);
}

void WorkDescriptorService::finishTask(const QString& id, WorkDescriptor::State state,
                                       const QString& detail)
{
    auto it = tasks_.find(id);
    if (it == tasks_.end() || it->state != WorkDescriptor::State::Running)
        return;
    it->state = state;
    it->detail = detail;
    it->progress = state == WorkDescriptor::State::Completed ? std::optional<int>(100) : std::nullopt;
    it->finishedAt = QDateTime::currentDateTimeUtc();
    emit tasksChanged();
}

QVector<WorkDescriptor> WorkDescriptorService::tasks() const
{
    QVector<WorkDescriptor> result;
    result.reserve(tasks_.size());
    for (auto it = tasks_.cbegin(); it != tasks_.cend(); ++it)
        result.append(it.value());
    std::sort(result.begin(), result.end(), [](const WorkDescriptor& left, const WorkDescriptor& right) {
        return left.startedAt < right.startedAt;
    });
    return result;
}

std::optional<int> WorkDescriptorService::progressForTask(const QString& id) const
{
    QSet<QString> visiting;
    return progressForTask(id, visiting);
}

std::optional<int> WorkDescriptorService::progressForTask(const QString& id,
                                                          QSet<QString>& visiting) const
{
    const auto it = tasks_.constFind(id);
    if (it == tasks_.cend())
        return std::nullopt;
    if (it->state == WorkDescriptor::State::Completed)
        return 100;
    if (it->state == WorkDescriptor::State::Failed)
        return std::nullopt;
    if (it->progress.has_value())
        return it->progress;
    if (visiting.contains(id))
        return std::nullopt;
    visiting.insert(id);

    QVector<int> childProgress;
    for (auto child = tasks_.cbegin(); child != tasks_.cend(); ++child) {
        if (child->parentId == id) {
            const std::optional<int> progress = progressForTask(child.key(), visiting);
            if (!progress.has_value()) {
                visiting.remove(id);
                return std::nullopt;
            }
            childProgress.append(*progress);
        }
    }
    visiting.remove(id);

    if (!childProgress.isEmpty()) {
        int total = 0;
        for (int progress : childProgress)
            total += progress;
        return total / childProgress.size();
    }
    return it->progress;
}

void WorkDescriptorService::pruneFinishedTasks()
{
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-12);
    QSet<QString> removeIds;
    for (auto it = tasks_.cbegin(); it != tasks_.cend(); ++it) {
        if (it->parentId.isEmpty() && it->state != WorkDescriptor::State::Running &&
            it->finishedAt.isValid() && it->finishedAt < cutoff)
            removeIds.insert(it.key());
    }
    if (removeIds.isEmpty())
        return;

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = tasks_.cbegin(); it != tasks_.cend(); ++it) {
            if (removeIds.contains(it->parentId) && !removeIds.contains(it.key()) &&
                it->state != WorkDescriptor::State::Running) {
                removeIds.insert(it.key());
                changed = true;
            }
        }
    }
    for (const QString& id : removeIds)
        tasks_.remove(id);
    emit tasksChanged();
}

} // namespace productivity::services
