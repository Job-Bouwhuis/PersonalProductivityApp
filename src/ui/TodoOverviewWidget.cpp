#include "TodoOverviewWidget.h"

#include "MailIndexService.h"
#include "NotesService.h"
#include "WorkDescriptorService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace productivity::ui {
namespace {

services::TodoFilter filterFromData(const QVariant& value)
{
    return static_cast<services::TodoFilter>(value.toInt());
}

QString linkDisplay(const services::TodoRecord& todo)
{
    if (todo.linkType.isEmpty())
        return QStringLiteral("—");
    if (!todo.linkLabel.isEmpty())
        return todo.linkLabel;
    if (todo.linkType == QLatin1String("note"))
        return QStringLiteral("Persistent note");
    return todo.linkTarget;
}

bool editDialog(QWidget* parent,
                services::TodoRecord& todo,
                const services::NotesService& notesService,
                const services::MailIndexService& mailIndexService)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(todo.id.isEmpty() ? QStringLiteral("New TODO") : QStringLiteral("Edit TODO"));
    dialog.resize(520, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* title = new QLineEdit(todo.title, &dialog);
    title->setPlaceholderText(QStringLiteral("What needs to be done?"));
    auto* description = new QPlainTextEdit(&dialog);
    description->setPlainText(todo.description);
    auto* hasDeadline = new QCheckBox(QStringLiteral("Set a deadline"), &dialog);
    hasDeadline->setChecked(todo.deadline.isValid());
    auto* deadline = new QDateTimeEdit(todo.deadline.isValid()
        ? todo.deadline.toLocalTime() : QDateTime::currentDateTime().addSecs(3600), &dialog);
    deadline->setCalendarPopup(true);
    deadline->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    deadline->setEnabled(hasDeadline->isChecked());

    auto* linkType = new QComboBox(&dialog);
    linkType->addItem(QStringLiteral("No link"), QString());
    linkType->addItem(QStringLiteral("Website"), QStringLiteral("website"));
    linkType->addItem(QStringLiteral("Local file"), QStringLiteral("file"));
    linkType->addItem(QStringLiteral("Persistent note"), QStringLiteral("note"));
    linkType->addItem(QStringLiteral("Quick note"), QStringLiteral("quick_note"));
    linkType->addItem(QStringLiteral("Email message"), QStringLiteral("email"));
    int linkIndex = linkType->findData(todo.linkType);
    linkType->setCurrentIndex(linkIndex < 0 ? 0 : linkIndex);
    auto* target = new QLineEdit(todo.linkTarget, &dialog);
    auto* note = new QComboBox(&dialog);
    note->addItem(QStringLiteral("Choose a note…"), QString());
    for (const auto& record : notesService.persistentNotes())
        note->addItem(record.title, record.id);
    for (const auto& record : notesService.quickNotes())
        note->addItem(QStringLiteral("Quick note: %1").arg(record.content.simplified().left(60)), record.id);
    const int noteIndex = note->findData(todo.linkTarget);
    if (noteIndex >= 0)
        note->setCurrentIndex(noteIndex);
    auto* email = new QComboBox(&dialog);
    email->setEditable(true);
    email->setInsertPolicy(QComboBox::NoInsert);
    email->setPlaceholderText(QStringLiteral("Search indexed email…"));
    email->completer()->setFilterMode(Qt::MatchContains);
    email->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    email->addItem(QStringLiteral("Choose an email…"), QString());
    for (const auto& message : mailIndexService.allMessages()) {
        const QString sender = message.senderName.isEmpty() ? message.senderAddress : message.senderName;
        const QString subject = message.subject.isEmpty() ? QStringLiteral("(no subject)") : message.subject;
        const QString label = sender.isEmpty() ? subject : sender + QStringLiteral(" — ") + subject;
        email->addItem(label, message.messageId);
    }
    if (!todo.linkTarget.isEmpty() && todo.linkType == QLatin1String("email") &&
        email->findData(todo.linkTarget) < 0) {
        email->addItem(todo.linkLabel.isEmpty() ? QStringLiteral("Email no longer indexed") : todo.linkLabel,
                       todo.linkTarget);
    }
    const int emailIndex = email->findData(todo.linkTarget);
    if (emailIndex >= 0)
        email->setCurrentIndex(emailIndex);
    auto* browse = new QPushButton(QStringLiteral("Browse…"), &dialog);
    auto* targetRow = new QWidget(&dialog);
    auto* targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->addWidget(target, 1);
    targetLayout->addWidget(browse);
    form->addRow(QStringLiteral("Title"), title);
    form->addRow(QStringLiteral("Description"), description);
    form->addRow(QString(), hasDeadline);
    form->addRow(QStringLiteral("Deadline"), deadline);
    form->addRow(QStringLiteral("Link type"), linkType);
    form->addRow(QStringLiteral("Link"), targetRow);
    form->addRow(QStringLiteral("Note"), note);
    form->addRow(QStringLiteral("Email"), email);
    layout->addLayout(form);

    auto updateLinkControls = [=] {
        const QString type = linkType->currentData().toString();
        targetRow->setVisible(type == QLatin1String("website") || type == QLatin1String("file"));
        note->setVisible(type == QLatin1String("note") || type == QLatin1String("quick_note"));
        email->setVisible(type == QLatin1String("email"));
        browse->setVisible(type == QLatin1String("file"));
    };
    QObject::connect(linkType, &QComboBox::currentIndexChanged, &dialog, updateLinkControls);
    QObject::connect(hasDeadline, &QCheckBox::toggled, deadline, &QDateTimeEdit::setEnabled);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, target] {
        const QString path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("Choose a file"), target->text());
        if (!path.isEmpty())
            target->setText(path);
    });
    updateLinkControls();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    todo.title = title->text().trimmed();
    todo.description = description->toPlainText();
    todo.deadline = hasDeadline->isChecked() ? deadline->dateTime().toUTC() : QDateTime();
    todo.linkType = linkType->currentData().toString();
    if (todo.linkType == QLatin1String("note") || todo.linkType == QLatin1String("quick_note")) {
        todo.linkTarget = note->currentData().toString();
        todo.linkLabel = note->currentText();
    } else if (todo.linkType == QLatin1String("email")) {
        todo.linkTarget = email->currentData().toString();
        todo.linkLabel = email->currentText();
    } else {
        todo.linkTarget = todo.linkType.isEmpty() ? QString() : target->text().trimmed();
        todo.linkLabel = todo.linkTarget;
    }
    if (todo.title.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("TODO title required"), QStringLiteral("Enter a title for this TODO."));
        return false;
    }
    if (todo.linkType == QLatin1String("email") &&
        (email->currentIndex() <= 0 || email->currentData().toString().isEmpty())) {
        QMessageBox::warning(parent, QStringLiteral("Email link required"),
                             QStringLiteral("Choose an indexed email from the list."));
        return false;
    }
    if (!todo.linkType.isEmpty() && todo.linkTarget.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Link required"), QStringLiteral("Choose or enter a link target."));
        return false;
    }
    return true;
}

} // namespace

TodoOverviewWidget::TodoOverviewWidget(services::TodoService& todoService,
                                       services::NotesService& notesService,
                                       services::MailIndexService& mailIndexService,
                                       services::WorkDescriptorService& workService,
                                       std::function<void(const QString&, bool)> openNote,
                                       std::function<void(const QString&)> openEmail,
                                       QWidget* parent)
    : QWidget(parent)
    , todoService_(todoService)
    , notesService_(notesService)
    , mailIndexService_(mailIndexService)
    , workService_(workService)
    , openNote_(std::move(openNote))
    , openEmail_(std::move(openEmail))
{
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    searchInput_ = new QLineEdit(this);
    searchInput_->setPlaceholderText(QStringLiteral("Search TODOs"));
    searchInput_->setClearButtonEnabled(true);
    filterInput_ = new QComboBox(this);
    filterInput_->addItem(QStringLiteral("All"), static_cast<int>(services::TodoFilter::All));
    filterInput_->addItem(QStringLiteral("Upcoming"), static_cast<int>(services::TodoFilter::Upcoming));
    filterInput_->addItem(QStringLiteral("Overdue"), static_cast<int>(services::TodoFilter::Overdue));
    filterInput_->addItem(QStringLiteral("Completed"), static_cast<int>(services::TodoFilter::Completed));
    filterInput_->addItem(QStringLiteral("Unscheduled"), static_cast<int>(services::TodoFilter::Unscheduled));
    controls->addWidget(searchInput_, 1);
    controls->addWidget(filterInput_);
    layout->addLayout(controls);

    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("TODO"), QStringLiteral("Deadline"), QStringLiteral("Status"), QStringLiteral("Link")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    auto* actions = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("New TODO"), this);
    auto* edit = new QPushButton(QStringLiteral("Edit"), this);
    auto* complete = new QPushButton(QStringLiteral("Complete / Reopen"), this);
    auto* open = new QPushButton(QStringLiteral("Open link"), this);
    auto* remove = new QPushButton(QStringLiteral("Delete"), this);
    actions->addWidget(add);
    actions->addWidget(edit);
    actions->addWidget(complete);
    actions->addWidget(open);
    actions->addWidget(remove);
    actions->addStretch();
    layout->addLayout(actions);

    connect(searchInput_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(filterInput_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(add, &QPushButton::clicked, this, [this] { createTodo(); });
    connect(edit, &QPushButton::clicked, this, [this] { editTodo(); });
    connect(complete, &QPushButton::clicked, this, [this] { toggleCompletion(); });
    connect(open, &QPushButton::clicked, this, [this] { openLink(); });
    connect(remove, &QPushButton::clicked, this, [this] { deleteTodo(); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this] { openLink(); });
    refresh();
}

services::TodoRecord TodoOverviewWidget::selectedTodo() const
{
    const int row = table_->currentRow();
    if (row < 0)
        return {};
    const QString id = table_->item(row, 0)->data(Qt::UserRole).toString();
    for (const auto& todo : todoService_.todos()) {
        if (todo.id == id)
            return todo;
    }
    return {};
}

void TodoOverviewWidget::refresh(const QString& preferredId)
{
    const QString selectedId = preferredId.isEmpty() && table_->currentRow() >= 0
        ? table_->item(table_->currentRow(), 0)->data(Qt::UserRole).toString() : preferredId;
    const auto items = todoService_.todos(filterFromData(filterInput_->currentData()), searchInput_->text());
    table_->setRowCount(items.size());
    int selectRow = -1;
    for (int row = 0; row < items.size(); ++row) {
        const auto& todo = items.at(row);
        auto* title = new QTableWidgetItem(todo.title);
        title->setData(Qt::UserRole, todo.id);
        title->setToolTip(todo.description);
        table_->setItem(row, 0, title);
        QString when = QStringLiteral("Unscheduled");
        if (todo.deadline.isValid()) {
            when = todo.deadline.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            if (todo.status == services::TodoStatus::Open && todo.deadline < QDateTime::currentDateTimeUtc())
                when += QStringLiteral(" (overdue)");
        }
        table_->setItem(row, 1, new QTableWidgetItem(when));
        table_->setItem(row, 2, new QTableWidgetItem(todo.status == services::TodoStatus::Completed
            ? QStringLiteral("Completed") : QStringLiteral("Open")));
        table_->setItem(row, 3, new QTableWidgetItem(linkDisplay(todo)));
        if (todo.id == selectedId)
            selectRow = row;
    }
    if (selectRow >= 0)
        table_->selectRow(selectRow);
    else if (!items.isEmpty())
        table_->selectRow(0);
}

void TodoOverviewWidget::createTodo()
{
    services::TodoRecord todo;
    if (!editDialog(this, todo, notesService_, mailIndexService_))
        return;
    const QString workId = workService_.startTask(QStringLiteral("Create TODO"));
    const QString id = todoService_.saveTodo(todo);
    if (id.isEmpty()) {
        workService_.failTask(workId, QStringLiteral("The TODO or its link is invalid"));
        QMessageBox::warning(this, QStringLiteral("Could not save TODO"), QStringLiteral("The TODO or its link is invalid."));
        return;
    }
    workService_.completeTask(workId, QStringLiteral("TODO created"));
    refresh(id);
}

void TodoOverviewWidget::editTodo()
{
    auto todo = selectedTodo();
    if (todo.id.isEmpty())
        return;
    if (!editDialog(this, todo, notesService_, mailIndexService_))
        return;
    const QString workId = workService_.startTask(QStringLiteral("Save TODO changes"));
    const QString id = todoService_.saveTodo(todo);
    if (id.isEmpty()) {
        workService_.failTask(workId, QStringLiteral("The TODO or its link is invalid"));
        QMessageBox::warning(this, QStringLiteral("Could not save TODO"), QStringLiteral("The TODO or its link is invalid."));
        return;
    }
    workService_.completeTask(workId, QStringLiteral("TODO updated"));
    refresh(id);
}

void TodoOverviewWidget::toggleCompletion()
{
    auto todo = selectedTodo();
    if (todo.id.isEmpty())
        return;
    todo.status = todo.status == services::TodoStatus::Completed
        ? services::TodoStatus::Open : services::TodoStatus::Completed;
    const QString workId = workService_.startTask(todo.status == services::TodoStatus::Completed
                                                      ? QStringLiteral("Complete TODO")
                                                      : QStringLiteral("Reopen TODO"));
    const QString id = todoService_.saveTodo(todo);
    if (!id.isEmpty()) {
        workService_.completeTask(workId, todo.status == services::TodoStatus::Completed
                                              ? QStringLiteral("TODO completed") : QStringLiteral("TODO reopened"));
        refresh(id);
    } else {
        workService_.failTask(workId, QStringLiteral("Could not update TODO status"));
    }
}

void TodoOverviewWidget::deleteTodo()
{
    const auto todo = selectedTodo();
    if (todo.id.isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete TODO"),
            QStringLiteral("Delete ‘%1’ permanently?").arg(todo.title),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const QString workId = workService_.startTask(QStringLiteral("Delete TODO"));
    if (todoService_.deleteTodo(todo.id))
        workService_.completeTask(workId, QStringLiteral("TODO deleted"));
    else
        workService_.failTask(workId, QStringLiteral("Could not delete TODO"));
    refresh();
}

void TodoOverviewWidget::openLink()
{
    const auto todo = selectedTodo();
    if (todo.linkType.isEmpty())
        return;
    const QString workId = workService_.startTask(QStringLiteral("Open TODO link"));
    if (todo.linkType == QLatin1String("note") || todo.linkType == QLatin1String("quick_note")) {
        if (openNote_)
            openNote_(todo.linkTarget, todo.linkType == QLatin1String("quick_note"));
        workService_.completeTask(workId, QStringLiteral("Opened linked note"));
    } else if (todo.linkType == QLatin1String("email")) {
        if (openEmail_) {
            openEmail_(todo.linkTarget);
            workService_.completeTask(workId, QStringLiteral("Opened linked email"));
        } else {
            workService_.failTask(workId, QStringLiteral("The mail viewer is unavailable"));
        }
    } else if (todo.linkType == QLatin1String("website")) {
        if (QDesktopServices::openUrl(QUrl(todo.linkTarget)))
            workService_.completeTask(workId, QStringLiteral("Opened website"));
        else
            workService_.failTask(workId, QStringLiteral("Could not open website"));
    } else if (todo.linkType == QLatin1String("file")) {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(todo.linkTarget)))
            workService_.completeTask(workId, QStringLiteral("Opened local file"));
        else
            workService_.failTask(workId, QStringLiteral("Could not open local file"));
    }
}

} // namespace productivity::ui
