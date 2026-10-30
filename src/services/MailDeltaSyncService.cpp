#include "MailDeltaSyncService.h"

#include "MailIndexService.h"
#include "MicrosoftGraphClient.h"
#include "ApplicationSettings.h"
#include "WorkDescriptorService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace productivity::services {

MailDeltaSyncService::MailDeltaSyncService(MicrosoftGraphClient& graphClient,
                                           MailIndexService& mailIndex,
                                           ApplicationSettings& settings,
                                           WorkDescriptorService& workService,
                                           QObject* parent)
    : QObject(parent)
    , graphClient_(graphClient)
    , mailIndex_(mailIndex)
    , settings_(settings)
    , workService_(workService)
{
    connect(&graphClient_, &MicrosoftGraphClient::requestFinished, this,
            [this](quint64 requestId, int httpStatus, const QByteArray& body) {
        if (requestId == readRequestId_) {
            readRequestId_ = 0;
            if (httpStatus < 200 || httpStatus >= 300) {
                fail(QStringLiteral("Microsoft Graph could not mark an ignored message as read (HTTP %1). The synchronization cursor was kept so the change can be retried.")
                         .arg(httpStatus));
                return;
            }
            QString error;
            if (!mailIndex_.setMessageReadState(readMessageId_, true, error)) {
                fail(error + QStringLiteral(" The synchronization cursor was kept so this message can be retried."));
                return;
            }
            readMessageId_.clear();
            markNextIgnoredSenderMessageRead();
            return;
        }
        if (requestId != requestId_ || phase_ == Phase::Idle)
            return;
        if (httpStatus == 410 && phase_ == Phase::ApplyingDeltas && !currentDeltaLink_.isEmpty()) {
            QString error;
            if (!mailIndex_.saveFolderDeltaLink(currentFolderId_, QStringLiteral(""), error)) {
                fail(error);
                return;
            }
            currentDeltaLink_.clear();
            emit progressChanged(QStringLiteral("Refreshing expired synchronization state for a mail folder…"));
            requestId_ = graphClient_.listFolderMessageDelta(currentFolderId_, {}, workTaskId_);
            return;
        }
        if (httpStatus != 200) {
            if (phase_ == Phase::MigratingIdentifiers && httpStatus == 403) {
                fail(QStringLiteral("Microsoft denied saved message ID migration (HTTP 403). Disconnect and reconnect Outlook to approve the delegated Microsoft Graph User.Read permission, then retry the sync."));
            } else {
                fail(phase_ == Phase::MigratingIdentifiers
                         ? QStringLiteral("Microsoft Graph could not migrate saved message IDs (HTTP %1). You can retry the sync.").arg(httpStatus)
                         : QStringLiteral("Microsoft Graph could not synchronize a mail folder (HTTP %1). You can retry the sync.").arg(httpStatus));
            }
            return;
        }
        if (phase_ == Phase::MigratingIdentifiers)
            handleIdentifierTranslation(body);
        else if (phase_ == Phase::ListingRoot || phase_ == Phase::ListingChildren)
            handleFolders(body);
        else if (phase_ == Phase::ApplyingDeltas)
            handleDelta(body);
    });
    connect(this, &MailDeltaSyncService::progressChanged, this, [this](const QString& detail) {
        if (!workTaskId_.isEmpty())
            workService_.updateTaskDetail(workTaskId_, detail);
    });
    connect(&graphClient_, &MicrosoftGraphClient::requestFailed, this,
            [this](quint64 requestId, const QString& error) {
        if (requestId == readRequestId_) {
            readRequestId_ = 0;
            fail(QStringLiteral("Could not mark an ignored message as read: %1 The synchronization cursor was kept so it can be retried.")
                     .arg(error));
            return;
        }
        if (requestId == requestId_ && phase_ != Phase::Idle)
            fail(error);
    });
}

bool MailDeltaSyncService::isRunning() const
{
    return phase_ != Phase::Idle;
}

void MailDeltaSyncService::start()
{
    if (isRunning()) {
        emit progressChanged(QStringLiteral("A mail synchronization is already running."));
        return;
    }
    workTaskId_ = workService_.startTask(QStringLiteral("Synchronize Outlook mail"));
    foldersWithChildren_.clear();
    foldersToSync_.clear();
    folderIndex_ = 0;
    changedCount_ = 0;
    removedCount_ = 0;
    untranslatableIdCount_ = 0;
    newMessages_ = {};
    ignoredMessagesToMarkRead_.clear();
    readRequestId_ = 0;
    readMessageId_.clear();
    pendingPageNextLink_.clear();
    pendingPageDeltaLink_.clear();
    currentFolderRescanRequired_ = false;
    currentFolderId_.clear();
    currentDeltaLink_.clear();
    translateNextIdentifierBatch();
}

void MailDeltaSyncService::translateNextIdentifierBatch()
{
    identifierMigrationBatch_ = mailIndex_.legacyMessageIds(1000);
    if (identifierMigrationBatch_.isEmpty()) {
        phase_ = Phase::ListingRoot;
        emit progressChanged(QStringLiteral("Finding mail folders for incremental synchronization…"));
        requestId_ = graphClient_.listRootMailFolders(workTaskId_);
        return;
    }
    phase_ = Phase::MigratingIdentifiers;
    emit progressChanged(QStringLiteral("Migrating %1 saved message ID(s) to stable IDs…")
                             .arg(identifierMigrationBatch_.size()));
        requestId_ = graphClient_.translateMessageIds(identifierMigrationBatch_, workTaskId_);
}

void MailDeltaSyncService::handleIdentifierTranslation(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject response = document.object();
    if (!document.isObject() || !response.value(QStringLiteral("value")).isArray()) {
        fail(QStringLiteral("Microsoft returned an unreadable message ID translation response."));
        return;
    }
    QHash<QString, QString> translatedIds;
    QStringList untranslatableIds;
    QSet<QString> accountedIds;
    const QJsonArray mappings = response.value(QStringLiteral("value")).toArray();
    for (const QJsonValue& value : mappings) {
        const QJsonObject mapping = value.toObject();
        const QString sourceId = mapping.value(QStringLiteral("sourceId")).toString();
        const QString targetId = mapping.value(QStringLiteral("targetId")).toString();
        if (sourceId.isEmpty() || !identifierMigrationBatch_.contains(sourceId))
            continue;
        accountedIds.insert(sourceId);
        if (!targetId.isEmpty()) {
            translatedIds.insert(sourceId, targetId);
        } else if (mapping.value(QStringLiteral("errorDetails")).isObject()) {
            untranslatableIds.append(sourceId);
        }
    }
    if (accountedIds.size() != identifierMigrationBatch_.size() ||
        translatedIds.size() + untranslatableIds.size() != identifierMigrationBatch_.size()) {
        fail(QStringLiteral("Microsoft returned an incomplete message ID translation response. No IDs in this batch were changed; retry the sync."));
        return;
    }
    QString error;
    if (!mailIndex_.migrateMessageIds(translatedIds, untranslatableIds, error)) {
        fail(error);
        return;
    }
    untranslatableIdCount_ += untranslatableIds.size();
    translateNextIdentifierBatch();
}

void MailDeltaSyncService::handleFolders(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject response = document.object();
    if (!document.isObject() || !response.value(QStringLiteral("value")).isArray()) {
        fail(QStringLiteral("Microsoft returned an unreadable mail-folder page."));
        return;
    }
    const QJsonArray folders = response.value(QStringLiteral("value")).toArray();
    for (const QJsonValue& value : folders) {
        const QJsonObject folder = value.toObject();
        const QString id = folder.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        foldersToSync_.append(id);
        if (folder.value(QStringLiteral("childFolderCount")).toInt() > 0)
            foldersWithChildren_.enqueue(id);
    }

    const QString nextLink = response.value(QStringLiteral("@odata.nextLink")).toString();
    if (!nextLink.isEmpty()) {
        requestId_ = graphClient_.listMailFoldersPage(nextLink, workTaskId_);
        return;
    }
    if (phase_ == Phase::ListingRoot)
        phase_ = Phase::ListingChildren;
    if (!foldersWithChildren_.isEmpty()) {
        requestChildFolderPage();
        return;
    }
    phase_ = Phase::ApplyingDeltas;
    requestNextFolderDelta();
}

void MailDeltaSyncService::requestChildFolderPage()
{
    if (foldersWithChildren_.isEmpty()) {
        phase_ = Phase::ApplyingDeltas;
        requestNextFolderDelta();
        return;
    }
    const QString folderId = foldersWithChildren_.dequeue();
    requestId_ = graphClient_.listChildMailFolders(folderId, workTaskId_);
}

void MailDeltaSyncService::requestNextFolderDelta()
{
    if (folderIndex_ >= foldersToSync_.size()) {
        phase_ = Phase::Idle;
        workService_.completeTask(workTaskId_, QStringLiteral("Mail synchronization complete"));
        if (!newMessages_.isEmpty())
            emit newMessagesReceived(newMessages_);
        emit finished(changedCount_, removedCount_, untranslatableIdCount_);
        return;
    }
    currentFolderId_ = foldersToSync_.at(folderIndex_++);
    currentFolderRescanRequired_ = mailIndex_.folderRescanRequired(currentFolderId_);
    currentDeltaLink_ = currentFolderRescanRequired_
        ? QString() : mailIndex_.folderDeltaLink(currentFolderId_);
    const int percent = foldersToSync_.isEmpty() ? 0
        : ((folderIndex_ - 1) * 100) / foldersToSync_.size();
    const QString detail = QStringLiteral("Synchronizing mail folder %1 of %2…")
                               .arg(folderIndex_).arg(foldersToSync_.size());
    workService_.updateTask(workTaskId_, detail, percent);
    emit progressChanged(detail);
    requestId_ = graphClient_.listFolderMessageDelta(currentFolderId_, currentDeltaLink_, workTaskId_);
}

void MailDeltaSyncService::handleDelta(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject response = document.object();
    if (!document.isObject() || !response.value(QStringLiteral("value")).isArray()) {
        fail(QStringLiteral("Microsoft returned an unreadable mail changes page."));
        return;
    }

    QJsonArray changedMessages;
    QStringList removedIds;
    const QJsonArray values = response.value(QStringLiteral("value")).toArray();
    for (const QJsonValue& value : values) {
        const QJsonObject item = value.toObject();
        if (item.contains(QStringLiteral("@removed"))) {
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                removedIds.append(id);
        } else {
            changedMessages.append(value);
        }
    }

    QString error;
    if (!changedMessages.isEmpty()) {
        QJsonArray newlyInsertedMessages;
        const int indexed = mailIndex_.indexMessages(changedMessages, error, &newlyInsertedMessages);
        if (indexed < 0) {
            fail(error);
            return;
        }
        changedCount_ += indexed;
        QSet<QString> newIds;
        for (const QJsonValue& value : newlyInsertedMessages)
            newIds.insert(value.toObject().value(QStringLiteral("id")).toString());
        QSet<QString> queuedReadIds;
        for (const QJsonValue& value : changedMessages) {
            const QJsonObject message = value.toObject();
            const QString id = message.value(QStringLiteral("id")).toString();
            const bool ignored = isIgnoredSender(message) || isIgnoredFolder(message);
            if (ignored && !message.value(QStringLiteral("isRead")).toBool() && !id.isEmpty() &&
                !queuedReadIds.contains(id)) {
                ignoredMessagesToMarkRead_.enqueue(id);
                queuedReadIds.insert(id);
            } else if (!ignored && newIds.contains(id)) {
                newMessages_.append(message);
            }
        }
    }
    if (!removedIds.isEmpty()) {
        if (!mailIndex_.removeMessages(removedIds, error)) {
            fail(error);
            return;
        }
        removedCount_ += removedIds.size();
    }

    pendingPageNextLink_ = response.value(QStringLiteral("@odata.nextLink")).toString();
    pendingPageDeltaLink_ = response.value(QStringLiteral("@odata.deltaLink")).toString();
    markNextIgnoredSenderMessageRead();
}

bool MailDeltaSyncService::isIgnoredSender(const QJsonObject& message) const
{
    const QString address = message.value(QStringLiteral("from")).toObject()
                                .value(QStringLiteral("emailAddress")).toObject()
                                .value(QStringLiteral("address")).toString().trimmed();
    if (address.isEmpty())
        return false;
    for (const QString& rule : settings_.ignoredSenderRules()) {
        if (address.compare(rule.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool MailDeltaSyncService::isIgnoredFolder(const QJsonObject& message) const
{
    const QString folderId = message.value(QStringLiteral("parentFolderId")).toString();
    if (folderId.isEmpty())
        return false;
    return settings_.ignoredFolderRules().contains(folderId);
}

void MailDeltaSyncService::markNextIgnoredSenderMessageRead()
{
    if (!ignoredMessagesToMarkRead_.isEmpty()) {
        readMessageId_ = ignoredMessagesToMarkRead_.dequeue();
        emit progressChanged(QStringLiteral("Marking mail covered by an ignore rule as read…"));
        readRequestId_ = graphClient_.setMessageRead(readMessageId_, true, workTaskId_);
        return;
    }
    finishDeltaPage();
}

void MailDeltaSyncService::finishDeltaPage()
{
    QString error;
    if (!pendingPageNextLink_.isEmpty()) {
        const QString nextLink = pendingPageNextLink_;
        pendingPageNextLink_.clear();
        pendingPageDeltaLink_.clear();
        requestId_ = graphClient_.listFolderMessageDeltaPage(nextLink, workTaskId_);
        return;
    }
    if (pendingPageDeltaLink_.isEmpty()) {
        fail(QStringLiteral("Microsoft did not provide a continuation token for a mail folder."));
        return;
    }
    if (!mailIndex_.saveFolderDeltaLink(currentFolderId_, pendingPageDeltaLink_, error)) {
        fail(error);
        return;
    }
    pendingPageDeltaLink_.clear();
    if (currentFolderRescanRequired_) {
        if (!mailIndex_.clearFolderRescanRequest(currentFolderId_, error)) {
            fail(error);
            return;
        }
        currentFolderRescanRequired_ = false;
    }
    requestNextFolderDelta();
}

void MailDeltaSyncService::fail(const QString& error)
{
    phase_ = Phase::Idle;
    workService_.failTask(workTaskId_, error);
    ignoredMessagesToMarkRead_.clear();
    readRequestId_ = 0;
    readMessageId_.clear();
    pendingPageNextLink_.clear();
    pendingPageDeltaLink_.clear();
    emit failed(error);
}

} // namespace productivity::services
