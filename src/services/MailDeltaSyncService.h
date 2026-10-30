#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QHash>
#include <QStringList>

namespace productivity::services {

class MailIndexService;
class MicrosoftGraphClient;
class ApplicationSettings;
class WorkDescriptorService;

class MailDeltaSyncService final : public QObject
{
    Q_OBJECT
public:
    MailDeltaSyncService(MicrosoftGraphClient& graphClient, MailIndexService& mailIndex,
                         ApplicationSettings& settings,
                         WorkDescriptorService& workService,
                         QObject* parent = nullptr);

    bool isRunning() const;
    void start();

signals:
    void progressChanged(const QString& status);
    void finished(int changedCount, int removedCount, int untranslatableIdCount);
    void newMessagesReceived(const QJsonArray& messages);
    void failed(const QString& error);

private:
    enum class Phase { Idle, MigratingIdentifiers, ListingRoot, ListingChildren, ApplyingDeltas };

    void translateNextIdentifierBatch();
    void handleIdentifierTranslation(const QByteArray& body);
    void requestChildFolderPage();
    void requestNextFolderDelta();
    void handleFolders(const QByteArray& body);
    void handleDelta(const QByteArray& body);
    bool isIgnoredSender(const QJsonObject& message) const;
    bool isIgnoredFolder(const QJsonObject& message) const;
    void markNextIgnoredSenderMessageRead();
    void finishDeltaPage();
    void fail(const QString& error);

    MicrosoftGraphClient& graphClient_;
    MailIndexService& mailIndex_;
    ApplicationSettings& settings_;
    WorkDescriptorService& workService_;
    QString workTaskId_;
    Phase phase_ = Phase::Idle;
    quint64 requestId_ = 0;
    QString currentFolderId_;
    QString currentDeltaLink_;
    QStringList identifierMigrationBatch_;
    QQueue<QString> foldersWithChildren_;
    QStringList foldersToSync_;
    int folderIndex_ = 0;
    int changedCount_ = 0;
    int removedCount_ = 0;
    int untranslatableIdCount_ = 0;
    QJsonArray newMessages_;
    bool currentFolderRescanRequired_ = false;
    QQueue<QString> ignoredMessagesToMarkRead_;
    quint64 readRequestId_ = 0;
    QString readMessageId_;
    QString pendingPageNextLink_;
    QString pendingPageDeltaLink_;
};

} // namespace productivity::services
