#pragma once

#include <QWidget>
#include <QHash>

class QLabel;
class QDialog;
class QLineEdit;
class QTableWidget;
class QTimer;

namespace productivity::services {
class MailDeltaSyncService;
class MailIndexService;
class MicrosoftGraphClient;
class ApplicationSettings;
class WorkDescriptorService;
}

namespace productivity::ui {

class MailListWidget final : public QWidget
{
    Q_OBJECT
public:
    MailListWidget(services::MailIndexService& mailIndex,
                   services::MailDeltaSyncService& mailSync,
                   services::MicrosoftGraphClient& graphClient,
                   services::ApplicationSettings& settings,
                   services::WorkDescriptorService& workService,
                   bool unreadOnly,
                   QWidget* parent = nullptr);
    void openMessageById(const QString& messageId);

private:
    void refreshMessages();

    services::MailIndexService& mailIndex_;
    services::MicrosoftGraphClient& graphClient_;
    services::ApplicationSettings& settings_;
    services::WorkDescriptorService& workService_;
    bool unreadOnly_ = false;
    QTableWidget* messages_ = nullptr;
    QLineEdit* searchInput_ = nullptr;
    QLabel* emptyState_ = nullptr;
    QLabel* syncStatus_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QHash<quint64, QDialog*> messageDialogs_;
};

} // namespace productivity::ui
