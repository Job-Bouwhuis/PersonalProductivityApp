#include "WorkActivityWidget.h"

#include "WorkDescriptorService.h"

#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace productivity::ui {

namespace {

QString stateText(services::WorkDescriptor::State state)
{
    switch (state) {
    case services::WorkDescriptor::State::Running: return QStringLiteral("In progress");
    case services::WorkDescriptor::State::Completed: return QStringLiteral("Completed");
    case services::WorkDescriptor::State::Failed: return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

void addProgressBar(QTreeWidgetItem* row, const services::WorkDescriptor& task,
                    services::WorkDescriptorService& workService)
{
    auto* bar = new QProgressBar;
    bar->setTextVisible(true);
    const auto progress = workService.progressForTask(task.id);
    if (!progress.has_value() && task.state == services::WorkDescriptor::State::Running) {
        bar->setRange(0, 0);
        bar->setFormat(QStringLiteral("Working…"));
    } else if (progress.has_value()) {
        bar->setRange(0, 100);
        bar->setValue(*progress);
        bar->setFormat(QStringLiteral("%p%"));
    } else {
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setFormat(QStringLiteral("Unavailable"));
    }
    row->treeWidget()->setItemWidget(row, 2, bar);
}

} // namespace

WorkActivityWidget::WorkActivityWidget(services::WorkDescriptorService& workService,
                                       QWidget* parent)
    : QWidget(parent)
    , workService_(workService)
{
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(
        QStringLiteral("Current application work and its subtasks appear here. Progress is indeterminate when the app cannot estimate completion."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    taskTree_ = new QTreeWidget(this);
    taskTree_->setColumnCount(3);
    taskTree_->setHeaderLabels({QStringLiteral("Work"), QStringLiteral("Status"), QStringLiteral("Progress")});
    taskTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    taskTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    taskTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    taskTree_->setRootIsDecorated(true);
    layout->addWidget(taskTree_);

    connect(&workService_, &services::WorkDescriptorService::tasksChanged,
            this, &WorkActivityWidget::refresh);
    refresh();
}

void WorkActivityWidget::refresh()
{
    const QVector<services::WorkDescriptor> tasks = workService_.tasks();
    taskTree_->clear();
    QVector<services::WorkDescriptor> roots;
    int activeRoots = 0;
    int rootPercentTotal = 0;
    bool indeterminate = false;
    for (const auto& task : tasks) {
        if (task.parentId.isEmpty()) {
            roots.append(task);
            if (task.state == services::WorkDescriptor::State::Running) {
                ++activeRoots;
                const auto progress = workService_.progressForTask(task.id);
                if (progress.has_value())
                    rootPercentTotal += *progress;
                else
                    indeterminate = true;
            }
        }
    }

    for (const auto& root : roots) {
        auto* row = new QTreeWidgetItem(taskTree_);
        row->setText(0, root.title);
        row->setText(1, root.detail.isEmpty() ? stateText(root.state) : root.detail);
        addProgressBar(row, root, workService_);
        addChildren(row, root.id, tasks);
        row->setExpanded(true);
    }
    taskTree_->expandAll();

    if (activeRoots == 0) {
        const auto latest = std::max_element(roots.cbegin(), roots.cend(), [](const auto& left, const auto& right) {
            return left.finishedAt < right.finishedAt;
        });
        if (latest == roots.cend() || !latest->finishedAt.isValid()) {
            emit summaryChanged(QStringLiteral("No background work"), false, 0);
        } else {
            const QString outcome = latest->state == services::WorkDescriptor::State::Completed
                ? QStringLiteral("completed") : QStringLiteral("failed");
            emit summaryChanged(QStringLiteral("Last: %1 — %2 (%3)")
                                    .arg(latest->title, latest->detail, outcome), false,
                                latest->state == services::WorkDescriptor::State::Completed ? 100 : 0);
        }
    } else if (activeRoots == 1) {
        const auto active = std::find_if(roots.cbegin(), roots.cend(), [](const auto& task) {
            return task.state == services::WorkDescriptor::State::Running;
        });
        const QString summary = active->detail.isEmpty() ? active->title
                                                         : QStringLiteral("%1 — %2").arg(active->title, active->detail);
        const auto progress = workService_.progressForTask(active->id);
        emit summaryChanged(summary, !progress.has_value(), progress.value_or(0));
    } else {
        emit summaryChanged(QStringLiteral("%1 application tasks are active").arg(activeRoots), indeterminate,
                            activeRoots > 0 ? rootPercentTotal / activeRoots : 0);
    }
}

void WorkActivityWidget::addChildren(QTreeWidgetItem* parent, const QString& parentId,
                                     const QVector<services::WorkDescriptor>& tasks)
{
    for (const auto& task : tasks) {
        if (task.parentId != parentId)
            continue;
        auto* row = new QTreeWidgetItem(parent);
        row->setText(0, task.title);
        row->setText(1, task.detail.isEmpty() ? stateText(task.state) : task.detail);
        addProgressBar(row, task, workService_);
        addChildren(row, task.id, tasks);
        row->setExpanded(true);
    }
}

} // namespace productivity::ui
