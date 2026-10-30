#include "MailIndexService.h"

#include <QDateTime>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>

#include <utility>

Q_LOGGING_CATEGORY(mailIndexLog, "application.outlook.index")

namespace productivity::services {

namespace {

QString nonNullString(const QJsonValue& value)
{
    return value.isString() ? value.toString() : QStringLiteral("");
}

} // namespace

MailIndexService::MailIndexService(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool MailIndexService::initialize()
{
    QSqlQuery query(database_);
    if (!database_.isOpen() || !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS outlook_message_index ("
            "message_id TEXT PRIMARY KEY,"
            "subject TEXT NOT NULL DEFAULT '',"
            "sender_name TEXT NOT NULL DEFAULT '',"
            "sender_address TEXT NOT NULL DEFAULT '',"
            "received_at_utc TEXT NOT NULL DEFAULT '',"
            "parent_folder_id TEXT NOT NULL DEFAULT '',"
            "id_format TEXT NOT NULL DEFAULT 'legacy',"
            "is_read INTEGER NOT NULL DEFAULT 0 CHECK(is_read IN (0, 1)),"
            "indexed_at_utc TEXT NOT NULL)")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS mail_sync_state ("
            "sync_key TEXT PRIMARY KEY,"
            "sync_value TEXT NOT NULL,"
            "updated_at_utc TEXT NOT NULL)"))) {
        qCCritical(mailIndexLog) << "Could not initialize Outlook message index:" << query.lastError().text();
        return false;
    }
    bool hasParentFolderId = false;
    bool hasIdFormat = false;
    if (!query.exec(QStringLiteral("PRAGMA table_info(outlook_message_index)"))) {
        qCCritical(mailIndexLog) << "Could not inspect Outlook message index schema:" << query.lastError().text();
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == QLatin1String("parent_folder_id")) {
            hasParentFolderId = true;
        }
        if (query.value(1).toString() == QLatin1String("id_format"))
            hasIdFormat = true;
    }
    if (!hasParentFolderId && !query.exec(QStringLiteral(
            "ALTER TABLE outlook_message_index ADD COLUMN parent_folder_id TEXT NOT NULL DEFAULT ''"))) {
        qCCritical(mailIndexLog) << "Could not add Outlook folder identity to the local index:" << query.lastError().text();
        return false;
    }
    if (!hasIdFormat && !query.exec(QStringLiteral(
            "ALTER TABLE outlook_message_index ADD COLUMN id_format TEXT NOT NULL DEFAULT 'legacy'"))) {
        qCCritical(mailIndexLog) << "Could not add message ID format to the local index:" << query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS outlook_message_index_received "
            "ON outlook_message_index(received_at_utc DESC)"))) {
        qCCritical(mailIndexLog) << "Could not index Outlook messages by received time:" << query.lastError().text();
        return false;
    }
    return true;
}

int MailIndexService::indexMessages(const QJsonArray& messages, QString& error,
                                    QJsonArray* newlyInsertedMessages)
{
    if (newlyInsertedMessages)
        *newlyInsertedMessages = {};
    if (!database_.isOpen()) {
        error = QStringLiteral("The local database is unavailable.");
        return -1;
    }
    if (!database_.transaction()) {
        error = QStringLiteral("Could not begin a local mailbox-index update.");
        qCWarning(mailIndexLog) << "Could not begin index transaction:" << database_.lastError().text();
        return -1;
    }

    QSqlQuery query(database_);
    QSqlQuery existingQuery(database_);
    existingQuery.prepare(QStringLiteral(
        "SELECT 1 FROM outlook_message_index WHERE message_id = :id"));
    query.prepare(QStringLiteral(
        "INSERT INTO outlook_message_index "
        "(message_id, subject, sender_name, sender_address, received_at_utc, parent_folder_id, id_format, is_read, indexed_at_utc) "
        "VALUES (:id, :subject, :sender_name, :sender_address, :received_at, :folder_id, 'immutable', :is_read, :indexed_at) "
        "ON CONFLICT(message_id) DO UPDATE SET "
        "subject = excluded.subject, sender_name = excluded.sender_name, "
        "sender_address = excluded.sender_address, received_at_utc = excluded.received_at_utc, "
        "parent_folder_id = excluded.parent_folder_id, id_format = 'immutable', "
        "is_read = excluded.is_read, indexed_at_utc = excluded.indexed_at_utc"));

    int indexed = 0;
    const QString indexedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QJsonValue& value : messages) {
        if (!value.isObject())
            continue;
        const QJsonObject message = value.toObject();
        const QString id = message.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        existingQuery.bindValue(QStringLiteral(":id"), id);
        if (!existingQuery.exec()) {
            const QSqlError databaseError = existingQuery.lastError();
            database_.rollback();
            error = QStringLiteral("Could not check whether a mail message is new.");
            qCWarning(mailIndexLog) << "Could not check indexed message identity:" << databaseError.text();
            return -1;
        }
        const bool isNew = !existingQuery.next();
        existingQuery.finish();
        const QJsonObject sender = message.value(QStringLiteral("from")).toObject()
                                      .value(QStringLiteral("emailAddress")).toObject();
        const QDateTime received = QDateTime::fromString(
            message.value(QStringLiteral("receivedDateTime")).toString(), Qt::ISODate).toUTC();

        query.bindValue(QStringLiteral(":id"), id);
        query.bindValue(QStringLiteral(":subject"), nonNullString(message.value(QStringLiteral("subject"))));
        query.bindValue(QStringLiteral(":sender_name"), nonNullString(sender.value(QStringLiteral("name"))));
        query.bindValue(QStringLiteral(":sender_address"), nonNullString(sender.value(QStringLiteral("address"))));
        query.bindValue(QStringLiteral(":received_at"), received.isValid()
                            ? received.toString(Qt::ISODateWithMs) : QStringLiteral(""));
        query.bindValue(QStringLiteral(":folder_id"), nonNullString(message.value(QStringLiteral("parentFolderId"))));
        query.bindValue(QStringLiteral(":is_read"), message.value(QStringLiteral("isRead")).toBool() ? 1 : 0);
        query.bindValue(QStringLiteral(":indexed_at"), indexedAt);
        if (!query.exec()) {
            const QSqlError databaseError = query.lastError();
            database_.rollback();
            error = QStringLiteral("Could not save messages to the local index.");
            qCWarning(mailIndexLog) << "Outlook message index update failed; database error:" << databaseError.text();
            return -1;
        }
        if (isNew && newlyInsertedMessages)
            newlyInsertedMessages->append(message);
        ++indexed;
    }

    if (!database_.commit()) {
        error = QStringLiteral("Could not finish saving the local mailbox index.");
        qCWarning(mailIndexLog) << "Could not commit Outlook message index:" << database_.lastError().text();
        database_.rollback();
        return -1;
    }
    qCInfo(mailIndexLog) << "Updated the local Outlook message index; messages processed:" << indexed;
    return indexed;
}

bool MailIndexService::setMessageReadState(const QString& messageId, bool isRead, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE outlook_message_index SET is_read = :is_read WHERE message_id = :id"));
    query.bindValue(QStringLiteral(":is_read"), isRead ? 1 : 0);
    query.bindValue(QStringLiteral(":id"), messageId);
    if (query.exec())
        return true;
    error = QStringLiteral("Could not update the local mail read state.");
    qCWarning(mailIndexLog) << "Could not update indexed message read state:" << query.lastError().text();
    return false;
}

int MailIndexService::indexedMessageCount() const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM outlook_message_index")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

QVector<IndexedMailSummary> MailIndexService::unreadMessages() const
{
    QVector<IndexedMailSummary> messages;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT message_id, subject, sender_name, sender_address, received_at_utc "
            "FROM outlook_message_index WHERE is_read = 0 "
            "ORDER BY received_at_utc DESC, message_id ASC"))) {
        qCWarning(mailIndexLog) << "Could not load unread messages from the local index:" << query.lastError().text();
        return messages;
    }
    while (query.next()) {
        IndexedMailSummary message;
        message.messageId = query.value(0).toString();
        message.subject = query.value(1).toString();
        message.senderName = query.value(2).toString();
        message.senderAddress = query.value(3).toString();
        message.receivedAtUtc = query.value(4).toString();
        messages.append(std::move(message));
    }
    return messages;
}

QVector<IndexedMailSummary> MailIndexService::allMessages() const
{
    QVector<IndexedMailSummary> messages;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT message_id, subject, sender_name, sender_address, received_at_utc "
            "FROM outlook_message_index "
            "ORDER BY received_at_utc DESC, message_id ASC"))) {
        qCWarning(mailIndexLog) << "Could not load messages from the local index:" << query.lastError().text();
        return messages;
    }
    while (query.next()) {
        IndexedMailSummary message;
        message.messageId = query.value(0).toString();
        message.subject = query.value(1).toString();
        message.senderName = query.value(2).toString();
        message.senderAddress = query.value(3).toString();
        message.receivedAtUtc = query.value(4).toString();
        messages.append(std::move(message));
    }
    return messages;
}

bool MailIndexService::initialMailboxScanComplete() const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT sync_value FROM mail_sync_state WHERE sync_key = :key"));
    query.bindValue(QStringLiteral(":key"), QStringLiteral("initial_mailbox_scan_complete"));
    return query.exec() && query.next() && query.value(0).toString() == QLatin1String("true");
}

bool MailIndexService::markInitialMailboxScanComplete(QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO mail_sync_state (sync_key, sync_value, updated_at_utc) "
        "VALUES (:key, 'true', :updated) "
        "ON CONFLICT(sync_key) DO UPDATE SET sync_value = 'true', updated_at_utc = excluded.updated_at_utc"));
    query.bindValue(QStringLiteral(":key"), QStringLiteral("initial_mailbox_scan_complete"));
    query.bindValue(QStringLiteral(":updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (query.exec())
        return true;
    error = QStringLiteral("Could not save the initial mailbox scan state.");
    qCWarning(mailIndexLog) << "Could not persist initial mailbox scan state:" << query.lastError().text();
    return false;
}

QString MailIndexService::folderDeltaLink(const QString& folderId) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT sync_value FROM mail_sync_state WHERE sync_key = :key"));
    query.bindValue(QStringLiteral(":key"), QStringLiteral("folder_delta:") + folderId);
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}

bool MailIndexService::saveFolderDeltaLink(const QString& folderId, const QString& deltaLink, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO mail_sync_state (sync_key, sync_value, updated_at_utc) "
        "VALUES (:key, :value, :updated) "
        "ON CONFLICT(sync_key) DO UPDATE SET sync_value = excluded.sync_value, updated_at_utc = excluded.updated_at_utc"));
    query.bindValue(QStringLiteral(":key"), QStringLiteral("folder_delta:") + folderId);
    query.bindValue(QStringLiteral(":value"), deltaLink);
    query.bindValue(QStringLiteral(":updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (query.exec())
        return true;
    error = QStringLiteral("Could not save mail synchronization progress.");
    qCWarning(mailIndexLog) << "Could not save mailbox folder delta link:" << query.lastError().text();
    return false;
}

bool MailIndexService::folderRescanRequired(const QString& folderId) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT sync_value FROM mail_sync_state WHERE sync_key = :key"));
    query.bindValue(QStringLiteral(":key"), QStringLiteral("folder_rescan_required:") + folderId);
    return query.exec() && query.next() && query.value(0).toString() == QLatin1String("true");
}

bool MailIndexService::requestFolderRescan(const QString& folderId, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO mail_sync_state (sync_key, sync_value, updated_at_utc) "
        "VALUES (:key, 'true', :updated) "
        "ON CONFLICT(sync_key) DO UPDATE SET sync_value = 'true', updated_at_utc = excluded.updated_at_utc"));
    query.bindValue(QStringLiteral(":key"), QStringLiteral("folder_rescan_required:") + folderId);
    query.bindValue(QStringLiteral(":updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (query.exec())
        return true;
    error = QStringLiteral("Could not queue the folder for synchronization.");
    qCWarning(mailIndexLog) << "Could not request a folder reconciliation scan:" << query.lastError().text();
    return false;
}

bool MailIndexService::requestAllFolderRescans(QString& error)
{
    QSqlQuery folders(database_);
    if (!folders.exec(QStringLiteral(
            "SELECT DISTINCT parent_folder_id FROM outlook_message_index "
            "WHERE parent_folder_id != ''"))) {
        error = QStringLiteral("Could not find indexed folders to reconcile.");
        qCWarning(mailIndexLog) << "Could not list folders for rule reconciliation:" << folders.lastError().text();
        return false;
    }
    QStringList folderIds;
    while (folders.next())
        folderIds.append(folders.value(0).toString());
    folders.finish();
    if (folderIds.isEmpty())
        return true;
    if (!database_.transaction()) {
        error = QStringLiteral("Could not queue folders for rule reconciliation.");
        qCWarning(mailIndexLog) << "Could not begin folder reconciliation request:" << database_.lastError().text();
        return false;
    }

    QSqlQuery request(database_);
    request.prepare(QStringLiteral(
        "INSERT INTO mail_sync_state (sync_key, sync_value, updated_at_utc) "
        "VALUES (:key, 'true', :updated) "
        "ON CONFLICT(sync_key) DO UPDATE SET sync_value = 'true', updated_at_utc = excluded.updated_at_utc"));
    const QString updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QString& folderId : folderIds) {
        request.bindValue(QStringLiteral(":key"), QStringLiteral("folder_rescan_required:") + folderId);
        request.bindValue(QStringLiteral(":updated"), updatedAt);
        if (!request.exec()) {
            const QSqlError databaseError = request.lastError();
            database_.rollback();
            error = QStringLiteral("Could not queue every folder for rule reconciliation.");
            qCWarning(mailIndexLog) << "Could not queue indexed folder reconciliation:" << databaseError.text();
            return false;
        }
        request.finish();
    }
    if (database_.commit())
        return true;
    database_.rollback();
    error = QStringLiteral("Could not finish queuing folders for rule reconciliation.");
    qCWarning(mailIndexLog) << "Could not commit folder reconciliation request:" << database_.lastError().text();
    return false;
}

bool MailIndexService::clearFolderRescanRequest(const QString& folderId, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM mail_sync_state WHERE sync_key = :key"));
    query.bindValue(QStringLiteral(":key"), QStringLiteral("folder_rescan_required:") + folderId);
    if (query.exec())
        return true;
    error = QStringLiteral("Could not finish reconciling the folder's saved rule.");
    qCWarning(mailIndexLog) << "Could not clear a completed folder rescan request:" << query.lastError().text();
    return false;
}

bool MailIndexService::removeMessages(const QStringList& messageIds, QString& error)
{
    if (messageIds.isEmpty())
        return true;
    if (!database_.transaction()) {
        error = QStringLiteral("Could not begin removing deleted mail from the local index.");
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM outlook_message_index WHERE message_id = :id"));
    for (const QString& id : messageIds) {
        query.bindValue(QStringLiteral(":id"), id);
        if (!query.exec()) {
            const QSqlError databaseError = query.lastError();
            database_.rollback();
            error = QStringLiteral("Could not remove deleted mail from the local index.");
            qCWarning(mailIndexLog) << "Could not apply mailbox deletion delta:" << databaseError.text();
            return false;
        }
    }
    if (database_.commit())
        return true;
    error = QStringLiteral("Could not finish removing deleted mail from the local index.");
    database_.rollback();
    return false;
}

QStringList MailIndexService::legacyMessageIds(int limit) const
{
    QStringList ids;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT message_id FROM outlook_message_index WHERE id_format != 'immutable' LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), qBound(1, limit, 1000));
    if (!query.exec()) {
        qCWarning(mailIndexLog) << "Could not read legacy message IDs for migration:" << query.lastError().text();
        return ids;
    }
    while (query.next())
        ids.append(query.value(0).toString());
    return ids;
}

bool MailIndexService::migrateMessageIds(const QHash<QString, QString>& translatedIds,
                                        const QStringList& untranslatableIds, QString& error)
{
    if (translatedIds.isEmpty() && untranslatableIds.isEmpty())
        return true;
    if (!database_.transaction()) {
        error = QStringLiteral("Could not begin message ID migration.");
        return false;
    }

    QSqlQuery targetExists(database_);
    targetExists.prepare(QStringLiteral("SELECT 1 FROM outlook_message_index WHERE message_id = :id"));
    QSqlQuery removeLegacy(database_);
    removeLegacy.prepare(QStringLiteral("DELETE FROM outlook_message_index WHERE message_id = :id AND id_format != 'immutable'"));
    QSqlQuery updateLegacy(database_);
    updateLegacy.prepare(QStringLiteral(
        "UPDATE outlook_message_index SET message_id = :target, id_format = 'immutable' "
        "WHERE message_id = :source AND id_format != 'immutable'"));

    QSqlQuery removeUntranslatable(database_);
    removeUntranslatable.prepare(QStringLiteral(
        "DELETE FROM outlook_message_index WHERE message_id = :source AND id_format != 'immutable'"));

    for (const QString& sourceId : untranslatableIds) {
        removeUntranslatable.bindValue(QStringLiteral(":source"), sourceId);
        if (!removeUntranslatable.exec()) {
            const QSqlError databaseError = removeUntranslatable.lastError();
            database_.rollback();
            error = QStringLiteral("Could not remove obsolete saved message IDs from the local index.");
            qCWarning(mailIndexLog) << "Could not remove untranslatable legacy message ID:" << databaseError.text();
            return false;
        }
    }

    for (auto it = translatedIds.cbegin(); it != translatedIds.cend(); ++it) {
        if (it.key().isEmpty() || it.value().isEmpty())
            continue;
        targetExists.bindValue(QStringLiteral(":id"), it.value());
        if (!targetExists.exec()) {
            database_.rollback();
            error = QStringLiteral("Could not check for duplicate messages during ID migration.");
            return false;
        }
        const bool targetAlreadyIndexed = targetExists.next();
        targetExists.finish();
        if (targetAlreadyIndexed) {
            removeLegacy.bindValue(QStringLiteral(":id"), it.key());
            if (!removeLegacy.exec()) {
                database_.rollback();
                error = QStringLiteral("Could not merge duplicate messages during ID migration.");
                return false;
            }
        } else {
            updateLegacy.bindValue(QStringLiteral(":target"), it.value());
            updateLegacy.bindValue(QStringLiteral(":source"), it.key());
            if (!updateLegacy.exec()) {
                const QSqlError databaseError = updateLegacy.lastError();
                database_.rollback();
                error = QStringLiteral("Could not update a message ID in the local index.");
                qCWarning(mailIndexLog) << "Message ID migration failed; database error:" << databaseError.text();
                return false;
            }
        }
    }
    if (database_.commit())
        return true;
    database_.rollback();
    error = QStringLiteral("Could not finish message ID migration.");
    return false;
}

} // namespace productivity::services
