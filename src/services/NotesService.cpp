#include "NotesService.h"

#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <utility>

Q_LOGGING_CATEGORY(notesLog, "application.notes")

namespace productivity::services {
namespace {

QString currentTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QList<NoteRecord> readNotes(QSqlQuery& query)
{
    QList<NoteRecord> notes;
    while (query.next()) {
        NoteRecord note;
        note.id = query.value(0).toString();
        note.title = query.value(1).toString();
        note.content = query.value(2).toString();
        note.updatedAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODate).toUTC();
        notes.append(note);
    }
    return notes;
}

bool hasTodoReference(QSqlDatabase database, const QString& linkType, const QString& target, bool& found)
{
    QSqlQuery table(database);
    if (!table.exec(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'todos'")))
        return false;
    const bool todoTableExists = table.next();
    table.finish();
    if (!todoTableExists) {
        found = false;
        return true;
    }

    QSqlQuery references(database);
    references.prepare(QStringLiteral(
        "SELECT 1 FROM todos WHERE link_type = :type AND link_target = :target LIMIT 1"));
    references.bindValue(QStringLiteral(":type"), linkType);
    references.bindValue(QStringLiteral(":target"), target);
    if (!references.exec())
        return false;
    found = references.next();
    references.finish();
    return true;
}

} // namespace

NotesService::NotesService(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool NotesService::initialize()
{
    QSqlQuery query(database_);
    if (!database_.isOpen() ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS persistent_notes ("
            "id TEXT PRIMARY KEY,"
            "title TEXT NOT NULL,"
            "markdown TEXT NOT NULL,"
            "created_at_utc TEXT NOT NULL,"
            "updated_at_utc TEXT NOT NULL)")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS quick_notes ("
            "id TEXT PRIMARY KEY,"
            "content TEXT NOT NULL,"
            "created_at_utc TEXT NOT NULL)"))) {
        qCCritical(notesLog) << "Could not initialize notes storage:" << query.lastError().text();
        return false;
    }
    qCInfo(notesLog) << "Notes storage initialized";
    return true;
}

QList<NoteRecord> NotesService::persistentNotes(const QString& searchText) const
{
    QSqlQuery query(database_);
    if (searchText.trimmed().isEmpty()) {
        if (!query.exec(QStringLiteral(
                "SELECT id, title, markdown, updated_at_utc FROM persistent_notes "
                "ORDER BY updated_at_utc DESC"))) {
            qCWarning(notesLog) << "Could not load persistent notes:" << query.lastError().text();
            return {};
        }
    } else {
        query.prepare(QStringLiteral(
            "SELECT id, title, markdown, updated_at_utc FROM persistent_notes "
            "WHERE title LIKE :search OR markdown LIKE :search ORDER BY updated_at_utc DESC"));
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%") + searchText.trimmed() + QStringLiteral("%"));
        if (!query.exec()) {
            qCWarning(notesLog) << "Could not search persistent notes:" << query.lastError().text();
            return {};
        }
    }
    return readNotes(query);
}

QString NotesService::savePersistentNote(const QString& id, const QString& title, const QString& markdown)
{
    if (!database_.isOpen())
        return {};
    const QString noteId = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    const QString savedTitle = title.trimmed().isEmpty() ? QStringLiteral("Untitled note") : title.trimmed();
    const QString savedMarkdown = markdown.isNull() ? QStringLiteral("") : markdown;
    const QString now = currentTimestamp();
    QSqlQuery query(database_);

    if (id.isEmpty()) {
        query.prepare(QStringLiteral(
            "INSERT INTO persistent_notes (id, title, markdown, created_at_utc, updated_at_utc) "
            "VALUES (:id, :title, :markdown, :created, :updated)"));
        query.bindValue(QStringLiteral(":created"), now);
    } else {
        query.prepare(QStringLiteral(
            "UPDATE persistent_notes SET title = :title, markdown = :markdown, updated_at_utc = :updated "
            "WHERE id = :id"));
    }
    query.bindValue(QStringLiteral(":id"), noteId);
    query.bindValue(QStringLiteral(":title"), savedTitle);
    query.bindValue(QStringLiteral(":markdown"), savedMarkdown);
    query.bindValue(QStringLiteral(":updated"), now);
    if (!query.exec() || query.numRowsAffected() != 1) {
        qCWarning(notesLog) << "Could not save persistent note:" << query.lastError().text();
        return {};
    }
    return noteId;
}

bool NotesService::deletePersistentNote(const QString& id)
{
    if (!database_.transaction())
        return false;
    bool hasReferences = false;
    if (!hasTodoReference(database_, QStringLiteral("note"), id, hasReferences)) {
        qCWarning(notesLog) << "Could not check TODO references before deleting note" << id;
        database_.rollback();
        return false;
    }
    if (hasReferences) {
        qCWarning(notesLog) << "Cannot delete persistent note because a TODO links to it:" << id;
        database_.rollback();
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM persistent_notes WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        qCWarning(notesLog) << "Could not delete persistent note:" << query.lastError().text();
        database_.rollback();
        return false;
    }
    if (query.numRowsAffected() != 1) {
        database_.rollback();
        return false;
    }
    query.finish();
    if (!database_.commit()) {
        database_.rollback();
        return false;
    }
    return true;
}

QList<NoteRecord> NotesService::quickNotes() const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, '' AS title, content, created_at_utc FROM quick_notes ORDER BY created_at_utc DESC"))) {
        qCWarning(notesLog) << "Could not load quick notes:" << query.lastError().text();
        return {};
    }
    return readNotes(query);
}

QString NotesService::captureQuickNote(const QString& content)
{
    const QString noteContent = content.trimmed();
    if (noteContent.isEmpty() || !database_.isOpen())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO quick_notes (id, content, created_at_utc) VALUES (:id, :content, :created)"));
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":content"), noteContent);
    query.bindValue(QStringLiteral(":created"), currentTimestamp());
    if (!query.exec()) {
        qCWarning(notesLog) << "Could not capture quick note:" << query.lastError().text();
        return {};
    }
    return id;
}

bool NotesService::dismissQuickNote(const QString& id)
{
    if (!database_.transaction())
        return false;
    bool hasReferences = false;
    if (!hasTodoReference(database_, QStringLiteral("quick_note"), id, hasReferences)) {
        qCWarning(notesLog) << "Could not check TODO references before dismissing quick note" << id;
        database_.rollback();
        return false;
    }
    if (hasReferences) {
        qCWarning(notesLog) << "Cannot dismiss quick note while a TODO links to it:" << id;
        database_.rollback();
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM quick_notes WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        qCWarning(notesLog) << "Could not dismiss quick note:" << query.lastError().text();
        database_.rollback();
        return false;
    }
    if (query.numRowsAffected() != 1) {
        database_.rollback();
        return false;
    }
    query.finish();
    if (!database_.commit()) {
        database_.rollback();
        return false;
    }
    return true;
}

} // namespace productivity::services
