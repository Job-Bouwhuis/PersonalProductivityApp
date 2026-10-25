#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QTimer;

namespace productivity::services {
class NotesService;
}

namespace productivity::ui {

class NotesWidget final : public QWidget
{
public:
    explicit NotesWidget(services::NotesService& notesService, QWidget* parent = nullptr);
    void openPersistentNote(const QString& id);
    void openQuickNote(const QString& id);

private:
    void createPersistentNote();
    void deletePersistentNote();
    void savePersistentNote();
    void scheduleAutosave();
    void populatePersistentList(const QString& preferredId = {});
    void loadSelectedPersistentNote();
    void captureQuickNote();
    void refreshQuickNotes();
    void dismissSelectedQuickNote();
    void showPersistentStatus(const QString& text);

    services::NotesService& notesService_;
    QTabWidget* tabs_ = nullptr;
    QLineEdit* searchInput_ = nullptr;
    QListWidget* persistentList_ = nullptr;
    QLineEdit* titleInput_ = nullptr;
    QPlainTextEdit* markdownInput_ = nullptr;
    QLabel* persistentStatus_ = nullptr;
    QString currentPersistentId_;
    QTimer* autosaveTimer_ = nullptr;
    bool persistentDirty_ = false;
    bool loadingPersistentNote_ = false;

    QPlainTextEdit* quickInput_ = nullptr;
    QListWidget* quickList_ = nullptr;
    QLabel* quickPreview_ = nullptr;
};

} // namespace productivity::ui
