#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace productivity::services {
class WorkDescriptorService;
struct WorkDescriptor;
}

namespace productivity::ui {

class WorkActivityWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit WorkActivityWidget(services::WorkDescriptorService& workService,
                                QWidget* parent = nullptr);

signals:
    void summaryChanged(const QString& summary, bool indeterminate, int percent);

private:
    void refresh();
    void addChildren(QTreeWidgetItem* parent, const QString& parentId,
                     const QVector<services::WorkDescriptor>& tasks);

    services::WorkDescriptorService& workService_;
    QTreeWidget* taskTree_ = nullptr;
};

} // namespace productivity::ui
