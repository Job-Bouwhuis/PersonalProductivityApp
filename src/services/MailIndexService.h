#pragma once

#include <QJsonArray>
#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

namespace productivity::services {

struct IndexedMailSummary
{
    QString messageId;
    QString subject;
    QString senderName;
    QString senderAddress;
    QString receivedAtUtc;
};

class MailIndexService final
{
public:
    explicit MailIndexService(QSqlDatabase database);

    bool initialize();
    int indexMessages(const QJsonArray& messages, QString& error,
                      QJsonArray* newlyInsertedMessages = nullptr);
    bool setMessageReadState(const QString& messageId, bool isRead, QString& error);
    int indexedMessageCount() const;
    QVector<IndexedMailSummary> allMessages() const;
    QVector<IndexedMailSummary> unreadMessages() const;
    bool initialMailboxScanComplete() const;
    bool markInitialMailboxScanComplete(QString& error);
    QString folderDeltaLink(const QString& folderId) const;
    bool saveFolderDeltaLink(const QString& folderId, const QString& deltaLink, QString& error);
    bool folderRescanRequired(const QString& folderId) const;
    bool requestFolderRescan(const QString& folderId, QString& error);
    bool requestAllFolderRescans(QString& error);
    bool clearFolderRescanRequest(const QString& folderId, QString& error);
    bool removeMessages(const QStringList& messageIds, QString& error);
    QStringList legacyMessageIds(int limit = 1000) const;
    bool migrateMessageIds(const QHash<QString, QString>& translatedIds,
                          const QStringList& untranslatableIds, QString& error);

private:
    QSqlDatabase database_;
};

} // namespace productivity::services
