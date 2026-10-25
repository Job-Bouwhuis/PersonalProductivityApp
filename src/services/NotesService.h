#pragma once

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace productivity::services {

struct NoteRecord {
    QString id;
    QString title;
    QString content;
    QDateTime updatedAt;
};

class NotesService final
{
public:
    explicit NotesService(QSqlDatabase database);

    bool initialize();
    QList<NoteRecord> persistentNotes(const QString& searchText = {}) const;
    QString savePersistentNote(const QString& id, const QString& title, const QString& markdown);
    bool deletePersistentNote(const QString& id);

    QList<NoteRecord> quickNotes() const;
    QString captureQuickNote(const QString& content);
    bool dismissQuickNote(const QString& id);

private:
    QSqlDatabase database_;
};

} // namespace productivity::services
