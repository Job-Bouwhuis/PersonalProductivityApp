#include "TimerOverviewWidget.h"

#include "TimerService.h"
#include "WorkDescriptorService.h"

#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(timerUiLog, "application.ui.timers")

namespace productivity::ui {
namespace {

QString remainingText(const QDateTime& deadline, QDateTime now)
{
    const qint64 secondsRemaining = now.secsTo(deadline);
    if (secondsRemaining <= 0)
        return QStringLiteral("Due");

    const qint64 hours = secondsRemaining / 3600;
    const qint64 minutes = (secondsRemaining % 3600) / 60;
    const qint64 seconds = secondsRemaining % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString remainingText(qint64 remainingMilliseconds)
{
    const qint64 secondsRemaining = qMax<qint64>(0, remainingMilliseconds) / 1000;
    const qint64 hours = secondsRemaining / 3600;
    const qint64 minutes = (secondsRemaining % 3600) / 60;
    const qint64 seconds = secondsRemaining % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

TimerOverviewWidget::TimerOverviewWidget(services::TimerService& timerService,
                                         services::WorkDescriptorService& workService,
                                         QWidget* parent)
    : QWidget(parent)
    , timerService_(timerService)
    , workService_(workService)
{
    auto* rootLayout = new QVBoxLayout(this);
    auto* createLayout = new QHBoxLayout;

    titleInput_ = new QLineEdit(this);
    titleInput_->setPlaceholderText(QStringLiteral("Timer name (optional)"));
    titleInput_->setAccessibleName(QStringLiteral("Timer name"));
    createLayout->addWidget(titleInput_, 1);

    hoursInput_ = new QSpinBox(this);
    hoursInput_->setRange(0, 999);
    hoursInput_->setSuffix(QStringLiteral(" h"));
    hoursInput_->setAccessibleName(QStringLiteral("Hours"));
    createLayout->addWidget(hoursInput_);

    minutesInput_ = new QSpinBox(this);
    minutesInput_->setRange(0, 59);
    minutesInput_->setSuffix(QStringLiteral(" min"));
    minutesInput_->setAccessibleName(QStringLiteral("Minutes"));
    createLayout->addWidget(minutesInput_);

    secondsInput_ = new QSpinBox(this);
    secondsInput_->setRange(0, 59);
    secondsInput_->setSuffix(QStringLiteral(" s"));
    secondsInput_->setAccessibleName(QStringLiteral("Seconds"));
    createLayout->addWidget(secondsInput_);

    auto* startButton = new QPushButton(QStringLiteral("Start timer"), this);
    createLayout->addWidget(startButton);
    rootLayout->addLayout(createLayout);

    timerTable_ = new QTableWidget(0, 4, this);
    timerTable_->setHorizontalHeaderLabels({
        QStringLiteral("Timer"), QStringLiteral("Deadline"),
        QStringLiteral("Remaining"), QStringLiteral("Status")
    });
    timerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    timerTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    timerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    timerTable_->verticalHeader()->setVisible(false);
    timerTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    timerTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    timerTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    timerTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    rootLayout->addWidget(timerTable_, 1);

    auto* actionLayout = new QHBoxLayout;
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel selected"), this);
    auto* completeButton = new QPushButton(QStringLiteral("Mark complete"), this);
    auto* pauseButton = new QPushButton(QStringLiteral("Pause / Resume"), this);
    actionLayout->addWidget(cancelButton);
    actionLayout->addWidget(completeButton);
    actionLayout->addWidget(pauseButton);
    actionLayout->addStretch();
    rootLayout->addLayout(actionLayout);

    statusLabel_ = new QLabel(this);
    statusLabel_->setMinimumHeight(statusLabel_->sizeHint().height());
    rootLayout->addWidget(statusLabel_);

    connect(startButton, &QPushButton::clicked, this, [this] { addCountdown(); });
    connect(cancelButton, &QPushButton::clicked, this, [this] { cancelSelected(); });
    connect(completeButton, &QPushButton::clicked, this, [this] { completeSelected(); });
    connect(pauseButton, &QPushButton::clicked, this, [this] { togglePauseSelected(); });

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(1000);
    connect(refreshTimer_, &QTimer::timeout, this, [this] { refreshTimers(); });
    refreshTimer_->start();
    refreshTimers();
}

void TimerOverviewWidget::addCountdown()
{
    const int durationSeconds = hoursInput_->value() * 3600 +
        minutesInput_->value() * 60 + secondsInput_->value();
    if (durationSeconds <= 0) {
        showStatus(QStringLiteral("Enter a duration greater than zero."));
        return;
    }

    QString title = titleInput_->text().trimmed();
    if (title.isEmpty())
        title = QStringLiteral("Countdown timer");

    const QDateTime deadline = QDateTime::currentDateTimeUtc().addSecs(durationSeconds);
    const QString workId = workService_.startTask(QStringLiteral("Create countdown timer"));
    workService_.updateTask(workId, QStringLiteral("Saving timer…"));
    const QString eventId = timerService_.schedule(
        title, deadline, services::NotificationPolicy::Deadline);
    if (eventId.isEmpty()) {
        workService_.failTask(workId, QStringLiteral("Could not save the timer"));
        showStatus(QStringLiteral("Could not save the timer. See the application log for details."));
        return;
    }
    workService_.completeTask(workId, QStringLiteral("Timer started"));

    titleInput_->clear();
    setDefaultDuration(defaultDurationSeconds_);
    showStatus(QStringLiteral("Timer started."));
    refreshTimers();
}

void TimerOverviewWidget::setDefaultDuration(int seconds)
{
    if (seconds <= 0)
        return;
    defaultDurationSeconds_ = seconds;
    hoursInput_->setValue(seconds / 3600);
    minutesInput_->setValue((seconds % 3600) / 60);
    secondsInput_->setValue(seconds % 60);
}

void TimerOverviewWidget::cancelSelected()
{
    const QString eventId = selectedEventId();
    if (eventId.isEmpty()) {
        showStatus(QStringLiteral("Select a timer first."));
        return;
    }
    const QString workId = workService_.startTask(QStringLiteral("Cancel countdown timer"));
    const bool success = timerService_.cancel(eventId);
    if (success)
        workService_.completeTask(workId, QStringLiteral("Timer cancelled"));
    else
        workService_.failTask(workId, QStringLiteral("Could not cancel the timer"));
    showStatus(success ? QStringLiteral("Timer cancelled.")
                       : QStringLiteral("Could not cancel the selected timer."));
    refreshTimers();
}

void TimerOverviewWidget::completeSelected()
{
    const QString eventId = selectedEventId();
    if (eventId.isEmpty()) {
        showStatus(QStringLiteral("Select a timer first."));
        return;
    }
    const QString workId = workService_.startTask(QStringLiteral("Complete countdown timer"));
    const bool success = timerService_.complete(eventId);
    if (success)
        workService_.completeTask(workId, QStringLiteral("Timer marked complete"));
    else
        workService_.failTask(workId, QStringLiteral("Could not complete the timer"));
    showStatus(success ? QStringLiteral("Timer marked complete.")
                       : QStringLiteral("Could not complete the selected timer."));
    refreshTimers();
}

void TimerOverviewWidget::togglePauseSelected()
{
    const QString eventId = selectedEventId();
    if (eventId.isEmpty()) {
        showStatus(QStringLiteral("Select a timer first."));
        return;
    }
    bool wasPaused = false;
    for (const auto& event : timerService_.timerEvents()) {
        if (event.id == eventId) {
            wasPaused = event.paused;
            break;
        }
    }
    const QString workId = workService_.startTask(wasPaused ? QStringLiteral("Resume countdown timer")
                                                            : QStringLiteral("Pause countdown timer"));
    const bool success = wasPaused ? timerService_.resume(eventId) : timerService_.pause(eventId);
    if (success)
        workService_.completeTask(workId, wasPaused ? QStringLiteral("Timer resumed") : QStringLiteral("Timer paused"));
    else
        workService_.failTask(workId, QStringLiteral("Could not change the timer state"));
    showStatus(success
        ? (wasPaused ? QStringLiteral("Timer resumed.") : QStringLiteral("Timer paused."))
        : QStringLiteral("Could not change the selected timer."));
    refreshTimers();
}

void TimerOverviewWidget::refreshTimers()
{
    const QString previouslySelectedId = selectedEventId();
    const auto events = timerService_.timerEvents();
    timerTable_->setRowCount(events.size());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    int selectedRow = -1;

    for (qsizetype row = 0; row < events.size(); ++row) {
        const auto& event = events.at(row);
        auto* title = readOnlyItem(event.title);
        title->setData(Qt::UserRole, event.id);
        timerTable_->setItem(static_cast<int>(row), 0, title);
        const QDateTime shownDeadline = event.paused ? now.addMSecs(event.remainingMilliseconds) : event.deadline;
        timerTable_->setItem(static_cast<int>(row), 1,
                             readOnlyItem(shownDeadline.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        timerTable_->setItem(static_cast<int>(row), 2, readOnlyItem(event.paused
            ? remainingText(event.remainingMilliseconds) : remainingText(event.deadline, now)));
        timerTable_->setItem(static_cast<int>(row), 3,
                             readOnlyItem(event.paused ? QStringLiteral("Paused")
                                 : (event.deadline <= now ? QStringLiteral("Due") : QStringLiteral("Running"))));
        if (event.id == previouslySelectedId)
            selectedRow = static_cast<int>(row);
    }

    if (selectedRow >= 0)
        timerTable_->selectRow(selectedRow);
}

QString TimerOverviewWidget::selectedEventId() const
{
    const auto selectedRows = timerTable_->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return {};
    const auto* item = timerTable_->item(selectedRows.first().row(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void TimerOverviewWidget::showStatus(const QString& message)
{
    statusLabel_->setText(message);
    qCInfo(timerUiLog) << message;
}

} // namespace productivity::ui
