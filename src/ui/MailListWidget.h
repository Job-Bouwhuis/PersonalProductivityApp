#pragma once

#include <QWidget>
#include <QHash>
#include <QPointer>
#include <QQueue>

class QLabel;
class QDialog;
class QLineEdit;
class QPushButton;
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

signals:
    void ignoredSenderRulesChanged();

private:
    void refreshMessages();
    void startMarkAllUnreadAsRead();
    void sendNextMarkReadRequest();
    void finishMarkReadRequest(bool success, const QString& error = {});

    services::MailIndexService& mailIndex_;
    services::MicrosoftGraphClient& graphClient_;
    services::ApplicationSettings& settings_;
    services::WorkDescriptorService& workService_;
    bool unreadOnly_ = false;
    QTableWidget* messages_ = nullptr;
    QLineEdit* searchInput_ = nullptr;
    QLabel* emptyState_ = nullptr;
    QLabel* syncStatus_ = nullptr;
    QPushButton* markAllReadButton_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QHash<quint64, QDialog*> messageDialogs_;
    QHash<quint64, QString> viewerMarkReadMessages_;
    QHash<quint64, QPointer<QPushButton>> viewerMarkReadButtons_;
    QQueue<QString> pendingMarkReadIds_;
    quint64 markReadRequestId_ = 0;
    int markReadTotal_ = 0;
    int markReadSucceeded_ = 0;
    int markReadFailed_ = 0;
    QString markReadWorkTaskId_;
    QString currentMarkReadMessageId_;
};

} // namespace productivity::ui
