#include "NotesWidget.h"

#include "NotesService.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

Q_LOGGING_CATEGORY(notesUiLog, "application.ui.notes")

namespace productivity::ui {
namespace {

QWidget* makePersistentNotesPage(NotesWidget* owner,
                                 QLineEdit*& searchInput,
                                 QListWidget*& noteList,
                                 QLineEdit*& titleInput,
                                 QPlainTextEdit*& markdownInput,
                                 QLabel*& statusLabel,
                                 const std::function<void()>& onNew,
                                 const std::function<void()>& onDelete,
                                 const std::function<void()>& onSave)
{
    auto* page = new QWidget(owner);
    auto* layout = new QVBoxLayout(page);
    auto* splitter = new QSplitter(page);

    auto* listPane = new QWidget(splitter);
    auto* listLayout = new QVBoxLayout(listPane);
    searchInput = new QLineEdit(listPane);
    searchInput->setPlaceholderText(QStringLiteral("Search notes"));
    searchInput->setClearButtonEnabled(true);
    listLayout->addWidget(searchInput);

    noteList = new QListWidget(listPane);
    noteList->setAccessibleName(QStringLiteral("Persistent notes"));
    listLayout->addWidget(noteList, 1);
    auto* listActions = new QHBoxLayout;
    auto* newButton = new QPushButton(QStringLiteral("New note"), listPane);
    auto* deleteButton = new QPushButton(QStringLiteral("Delete"), listPane);
    listActions->addWidget(newButton);
    listActions->addWidget(deleteButton);
    listLayout->addLayout(listActions);

    auto* editorPane = new QWidget(splitter);
    auto* editorLayout = new QVBoxLayout(editorPane);
    titleInput = new QLineEdit(editorPane);
    titleInput->setPlaceholderText(QStringLiteral("Note title"));
    titleInput->setAccessibleName(QStringLiteral("Note title"));
    editorLayout->addWidget(titleInput);
    auto* markdownLabel = new QLabel(QStringLiteral("Markdown content"), editorPane);
    editorLayout->addWidget(markdownLabel);
    markdownInput = new QPlainTextEdit(editorPane);
    markdownInput->setPlaceholderText(QStringLiteral("Write your note using Markdown…"));
    markdownInput->setAccessibleName(QStringLiteral("Markdown note content"));
    editorLayout->addWidget(markdownInput, 1);
    auto* editorActions = new QHBoxLayout;
    statusLabel = new QLabel(editorPane);
    auto* saveButton = new QPushButton(QStringLiteral("Save now"), editorPane);
    editorActions->addWidget(statusLabel, 1);
    editorActions->addWidget(saveButton);
    editorLayout->addLayout(editorActions);

    splitter->addWidget(listPane);
    splitter->addWidget(editorPane);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    QObject::connect(newButton, &QPushButton::clicked, owner, onNew);
    QObject::connect(deleteButton, &QPushButton::clicked, owner, onDelete);
    QObject::connect(saveButton, &QPushButton::clicked, owner, onSave);
    return page;
}

QString quickNotePreview(const QString& content)
{
    QString preview = content.simplified();
    if (preview.size() > 90)
        preview = preview.left(87) + QStringLiteral("…");
    return preview;
}

} // namespace

NotesWidget::NotesWidget(services::NotesService& notesService, QWidget* parent)
    : QWidget(parent)
    , notesService_(notesService)
{
    auto* rootLayout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);

    auto* persistentPage = makePersistentNotesPage(
        this, searchInput_, persistentList_, titleInput_, markdownInput_, persistentStatus_,
        [this] { createPersistentNote(); },
        [this] { deletePersistentNote(); },
        [this] { savePersistentNote(); });
    tabs_->addTab(persistentPage, QStringLiteral("Persistent notes"));

    auto* quickPage = new QWidget(tabs_);
    auto* quickLayout = new QVBoxLayout(quickPage);
    quickInput_ = new QPlainTextEdit(quickPage);
    quickInput_->setMaximumHeight(100);
    quickInput_->setPlaceholderText(QStringLiteral("Capture a quick note…"));
    quickInput_->setAccessibleName(QStringLiteral("Quick note capture"));
    quickLayout->addWidget(quickInput_);
    auto* captureButton = new QPushButton(QStringLiteral("Capture quick note"), quickPage);
    quickLayout->addWidget(captureButton, 0, Qt::AlignLeft);

    auto* quickSplitter = new QSplitter(quickPage);
    quickList_ = new QListWidget(quickSplitter);
    quickList_->setAccessibleName(QStringLiteral("Quick notes"));
    quickPreview_ = new QLabel(QStringLiteral("Select a quick note to view it."), quickSplitter);
    quickPreview_->setWordWrap(true);
    quickPreview_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    quickPreview_->setMargin(8);
    quickSplitter->addWidget(quickList_);
    quickSplitter->addWidget(quickPreview_);
    quickSplitter->setStretchFactor(1, 1);
    quickLayout->addWidget(quickSplitter, 1);

    auto* quickActions = new QHBoxLayout;
    quickActions->addStretch();
    auto* dismissButton = new QPushButton(QStringLiteral("Dismiss selected quick note"), quickPage);
    quickActions->addWidget(dismissButton);
    quickLayout->addLayout(quickActions);
    tabs_->addTab(quickPage, QStringLiteral("Quick notes"));
    rootLayout->addWidget(tabs_);

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setSingleShot(true);
    autosaveTimer_->setInterval(700);
    connect(autosaveTimer_, &QTimer::timeout, this, [this] { savePersistentNote(); });
    connect(searchInput_, &QLineEdit::textChanged, this, [this] {
        savePersistentNote();
        populatePersistentList(currentPersistentId_);
    });
    connect(persistentList_, &QListWidget::currentRowChanged, this, [this](int) {
        if (loadingPersistentNote_)
            return;
        savePersistentNote();
        loadSelectedPersistentNote();
    });
    connect(titleInput_, &QLineEdit::textChanged, this, [this] { scheduleAutosave(); });
    connect(markdownInput_, &QPlainTextEdit::textChanged, this, [this] { scheduleAutosave(); });
    connect(captureButton, &QPushButton::clicked, this, [this] { captureQuickNote(); });
    connect(dismissButton, &QPushButton::clicked, this, [this] { dismissSelectedQuickNote(); });
    connect(quickList_, &QListWidget::currentRowChanged, this, [this](int row) {
        const auto* item = quickList_->item(row);
        if (!item) {
            quickPreview_->setText(QStringLiteral("Select a quick note to view it."));
            return;
        }
        const QString id = item->data(Qt::UserRole).toString();
        for (const auto& note : notesService_.quickNotes()) {
            if (note.id == id) {
                quickPreview_->setText(note.content);
                return;
            }
        }
    });

    populatePersistentList();
    refreshQuickNotes();
}

void NotesWidget::openPersistentNote(const QString& id)
{
    savePersistentNote();
    searchInput_->clear();
    populatePersistentList(id);
    for (int row = 0; row < persistentList_->count(); ++row) {
        if (persistentList_->item(row)->data(Qt::UserRole).toString() == id) {
            persistentList_->setCurrentRow(row);
            return;
        }
    }
}

void NotesWidget::openQuickNote(const QString& id)
{
    refreshQuickNotes();
    for (int row = 0; row < quickList_->count(); ++row) {
        if (quickList_->item(row)->data(Qt::UserRole).toString() == id) {
            tabs_->setCurrentIndex(1);
            quickList_->setCurrentRow(row);
            return;
        }
    }
}

void NotesWidget::createPersistentNote()
{
    savePersistentNote();
    const QString id = notesService_.savePersistentNote({}, QStringLiteral("Untitled note"), {});
    if (id.isEmpty()) {
        showPersistentStatus(QStringLiteral("Could not create the note. See the application log."));
        return;
    }
    searchInput_->clear();
    populatePersistentList(id);
    titleInput_->setFocus();
    titleInput_->selectAll();
    showPersistentStatus(QStringLiteral("New note created. Changes save automatically."));
}

void NotesWidget::deletePersistentNote()
{
    if (currentPersistentId_.isEmpty())
        return;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Delete note"),
        QStringLiteral("Delete this persistent note permanently?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    autosaveTimer_->stop();
    if (!notesService_.deletePersistentNote(currentPersistentId_)) {
        showPersistentStatus(QStringLiteral("Could not delete the note; a TODO may link to it."));
        return;
    }
    currentPersistentId_.clear();
    persistentDirty_ = false;
    populatePersistentList();
    showPersistentStatus(QStringLiteral("Note deleted."));
}

void NotesWidget::savePersistentNote()
{
    if (currentPersistentId_.isEmpty() || loadingPersistentNote_ || !persistentDirty_)
        return;
    autosaveTimer_->stop();
    const QString savedId = notesService_.savePersistentNote(
        currentPersistentId_, titleInput_->text(), markdownInput_->toPlainText());
    if (savedId.isEmpty()) {
        showPersistentStatus(QStringLiteral("Could not save the note. See the application log."));
        return;
    }
    persistentDirty_ = false;
    showPersistentStatus(QStringLiteral("Saved automatically."));
    for (int row = 0; row < persistentList_->count(); ++row) {
        auto* item = persistentList_->item(row);
        if (item->data(Qt::UserRole).toString() == savedId) {
            item->setText(titleInput_->text().trimmed().isEmpty()
                ? QStringLiteral("Untitled note")
                : titleInput_->text().trimmed());
            break;
        }
    }
}

void NotesWidget::scheduleAutosave()
{
    if (!currentPersistentId_.isEmpty() && !loadingPersistentNote_) {
        persistentDirty_ = true;
        autosaveTimer_->start();
    }
}

void NotesWidget::populatePersistentList(const QString& preferredId)
{
    loadingPersistentNote_ = true;
    const QString idToSelect = preferredId.isEmpty() ? currentPersistentId_ : preferredId;
    persistentList_->clear();
    int selectedRow = -1;
    const auto notes = notesService_.persistentNotes(searchInput_->text());
    for (qsizetype index = 0; index < notes.size(); ++index) {
        const auto& note = notes.at(index);
        auto* item = new QListWidgetItem(note.title, persistentList_);
        item->setData(Qt::UserRole, note.id);
        item->setToolTip(note.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        if (note.id == idToSelect)
            selectedRow = static_cast<int>(index);
    }
    if (selectedRow < 0 && !notes.isEmpty())
        selectedRow = 0;
    if (selectedRow >= 0)
        persistentList_->setCurrentRow(selectedRow);
    loadingPersistentNote_ = false;
    loadSelectedPersistentNote();
}

void NotesWidget::loadSelectedPersistentNote()
{
    const auto* item = persistentList_->currentItem();
    if (!item) {
        currentPersistentId_.clear();
        persistentDirty_ = false;
        const QSignalBlocker titleBlocker(titleInput_);
        const QSignalBlocker contentBlocker(markdownInput_);
        titleInput_->clear();
        markdownInput_->clear();
        return;
    }

    const QString id = item->data(Qt::UserRole).toString();
    for (const auto& note : notesService_.persistentNotes()) {
        if (note.id == id) {
            loadingPersistentNote_ = true;
            currentPersistentId_ = note.id;
            persistentDirty_ = false;
            titleInput_->setText(note.title);
            markdownInput_->setPlainText(note.content);
            loadingPersistentNote_ = false;
            return;
        }
    }
    currentPersistentId_.clear();
}

void NotesWidget::captureQuickNote()
{
    const QString id = notesService_.captureQuickNote(quickInput_->toPlainText());
    if (id.isEmpty()) {
        quickPreview_->setText(QStringLiteral("Enter a quick note before capturing it."));
        return;
    }
    quickInput_->clear();
    refreshQuickNotes();
    for (int row = 0; row < quickList_->count(); ++row) {
        if (quickList_->item(row)->data(Qt::UserRole).toString() == id) {
            quickList_->setCurrentRow(row);
            break;
        }
    }
}

void NotesWidget::refreshQuickNotes()
{
    const QString selectedId = quickList_->currentItem()
        ? quickList_->currentItem()->data(Qt::UserRole).toString()
        : QString();
    quickList_->clear();
    int selectedRow = -1;
    const auto notes = notesService_.quickNotes();
    for (qsizetype index = 0; index < notes.size(); ++index) {
        const auto& note = notes.at(index);
        auto* item = new QListWidgetItem(quickNotePreview(note.content), quickList_);
        item->setData(Qt::UserRole, note.id);
        item->setToolTip(note.content);
        if (note.id == selectedId)
            selectedRow = static_cast<int>(index);
    }
    if (selectedRow >= 0)
        quickList_->setCurrentRow(selectedRow);
    else if (!notes.isEmpty())
        quickList_->setCurrentRow(0);
    else
        quickPreview_->setText(QStringLiteral("Select a quick note to view it."));
}

void NotesWidget::dismissSelectedQuickNote()
{
    const auto* item = quickList_->currentItem();
    if (!item) {
        quickPreview_->setText(QStringLiteral("Select a quick note to dismiss it."));
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    if (!notesService_.dismissQuickNote(id)) {
        quickPreview_->setText(QStringLiteral("Could not dismiss this quick note; a TODO may link to it."));
        return;
    }
    refreshQuickNotes();
}

void NotesWidget::showPersistentStatus(const QString& text)
{
    persistentStatus_->setText(text);
    qCInfo(notesUiLog) << text;
}

} // namespace productivity::ui
