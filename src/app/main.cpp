#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QIcon>
#include <QLoggingCategory>
#include <QMenu>
#include <QJsonArray>
#include <QMainWindow>
#include <QLockFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QStatusBar>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QLabel>

#include <memory>

#include "logging/ApplicationLogger.h"
#include "notifications/NotificationCoordinator.h"
#include "persistence/ApplicationDatabase.h"
#include "DeadlineMonitor.h"
#include "ImportantSenderTodoService.h"
#include "NotesService.h"
#include "ApplicationSettings.h"
#include "MicrosoftGraphClient.h"
#include "MailIndexService.h"
#include "MailDeltaSyncService.h"
#include "OutlookAuthService.h"
#include "AutostartService.h"
#include "TodoService.h"
#include "TimerService.h"
#include "WorkDescriptorService.h"
#include "NotesWidget.h"
#include "SettingsWidget.h"
#include "TodoOverviewWidget.h"
#include "TimerOverviewWidget.h"
#include "MailListWidget.h"
#include "WorkActivityWidget.h"

Q_LOGGING_CATEGORY(startupLog, "application.startup")

namespace {

QWidget* unavailableServicePage(const QString& serviceName, QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    auto* message = new QLabel(QStringLiteral("%1 is unavailable in this session.").arg(serviceName), page);
    message->setWordWrap(true);
    auto* details = new QLabel(QStringLiteral("The rest of the application is still running. Check the application log for details."), page);
    details->setWordWrap(true);
    layout->addWidget(message);
    layout->addWidget(details);
    layout->addStretch();
    return page;
}

class ProductivityWindow final : public QMainWindow
{
public:
    void setHideToTray(bool enabled)
    {
        hideToTray = enabled;
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (hideToTray) {
            hide();
            event->ignore();
        } else {
            event->accept();
        }
    }

private:
    bool hideToTray = false;
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Personal Productivity Service");
    QApplication::setOrganizationName("PersonalProductivityApp");

    productivity::infrastructure::initializeApplicationLogging();

    const QString appDataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (appDataDirectory.isEmpty() || !QDir().mkpath(appDataDirectory)) {
        qCCritical(startupLog) << "Could not create application data directory";
        return 1;
    }
    QLockFile instanceLock(appDataDirectory + QStringLiteral("/application.lock"));
    if (!instanceLock.tryLock(0)) {
        qCInfo(startupLog) << "Another application instance is already running";
        return 0;
    }

    productivity::infrastructure::ApplicationDatabase database;
    if (!database.open())
        return 1;

    productivity::services::TimerService timerService(database.connection());
    const bool timersReady = timerService.initialize();
    productivity::services::NotesService notesService(database.connection());
    const bool notesReady = notesService.initialize();
    productivity::services::TodoService todoService(database.connection());
    const bool todosReady = todoService.initialize();
    if (!timersReady)
        qCWarning(startupLog) << "Timer service is unavailable; continuing with other services";
    if (!notesReady)
        qCWarning(startupLog) << "Notes service is unavailable; continuing with other services";
    if (!todosReady)
        qCWarning(startupLog) << "TODO service is unavailable; continuing with other services";
    productivity::services::ApplicationSettings settings;
    productivity::services::WorkDescriptorService workDescriptorService(&app);
    productivity::services::OutlookAuthService outlookAuthService(&app);
    productivity::services::MicrosoftGraphClient graphClient(settings, outlookAuthService,
                                                               workDescriptorService, &app);
    productivity::services::MailIndexService mailIndexService(database.connection());
    const bool mailIndexReady = mailIndexService.initialize();
    if (!mailIndexReady)
        qCWarning(startupLog) << "Outlook message indexing is unavailable";
    productivity::services::MailDeltaSyncService mailDeltaSync(graphClient, mailIndexService, settings,
                                                               workDescriptorService, &app);
    productivity::services::ImportantSenderTodoService importantSenderTodoService(
        settings, todoService, workDescriptorService);
    if (todosReady) {
        QObject::connect(&mailDeltaSync, &productivity::services::MailDeltaSyncService::newMessagesReceived,
                &app, [&importantSenderTodoService](const QJsonArray& messages) {
            importantSenderTodoService.createForNewMessages(messages);
        });
    }
    productivity::services::AutostartService autostartService;
    productivity::services::DeadlineMonitor* deadlineMonitorControl = nullptr;
    if (!autostartService.setEnabled(settings.startOnLogin()))
        qCWarning(startupLog) << "Could not apply the saved login startup preference";

    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    QSystemTrayIcon trayIcon(QIcon(":/app.svg"));
    trayIcon.setToolTip("Personal Productivity Service");
    productivity::infrastructure::NotificationCoordinator notificationCoordinator(&trayIcon,
                                                                                   workDescriptorService);

    ProductivityWindow window;
    window.setWindowTitle("Personal Productivity Service");
    window.setWindowIcon(QIcon(":/app.svg"));
    window.resize(900, 600);
    window.setHideToTray(trayAvailable && settings.closeToTray());
    auto* tabs = new QTabWidget(&window);
    productivity::ui::TimerOverviewWidget* timerWidget = nullptr;
    if (timersReady) {
        timerWidget = new productivity::ui::TimerOverviewWidget(timerService, workDescriptorService, tabs);
        timerWidget->setDefaultDuration(settings.defaultCountdownSeconds());
        tabs->addTab(timerWidget, QStringLiteral("Timers"));
    } else {
        tabs->addTab(unavailableServicePage(QStringLiteral("Countdown timers"), tabs), QStringLiteral("Timers"));
    }
    productivity::ui::NotesWidget* notesWidget = nullptr;
    productivity::ui::MailListWidget* allMailWidget = nullptr;
    QWidget* notesPage = nullptr;
    if (notesReady) {
        notesWidget = new productivity::ui::NotesWidget(notesService, workDescriptorService, tabs);
        notesPage = notesWidget;
    } else {
        notesPage = unavailableServicePage(QStringLiteral("Notes"), tabs);
    }
    if (todosReady) {
        tabs->addTab(new productivity::ui::TodoOverviewWidget(todoService, notesService, mailIndexService,
            workDescriptorService,
            [&window, tabs, notesWidget](const QString& noteId, bool quickNote) {
                if (!notesWidget)
                    return;
                tabs->setCurrentWidget(notesWidget);
                if (quickNote)
                    notesWidget->openQuickNote(noteId);
                else
                    notesWidget->openPersistentNote(noteId);
                window.showNormal();
                window.raise();
                window.activateWindow();
            }, [&window, tabs, &allMailWidget](const QString& messageId) {
                if (!allMailWidget)
                    return;
                tabs->setCurrentWidget(allMailWidget);
                allMailWidget->openMessageById(messageId);
                window.showNormal();
                window.raise();
                window.activateWindow();
            }, tabs), QStringLiteral("TODOs"));
    } else {
        tabs->addTab(unavailableServicePage(QStringLiteral("TODO manager"), tabs), QStringLiteral("TODOs"));
    }
    tabs->addTab(notesPage, QStringLiteral("Notes"));
    if (mailIndexReady) {
        auto* unreadMailWidget = new productivity::ui::MailListWidget(
            mailIndexService, mailDeltaSync, graphClient, settings, workDescriptorService, true, tabs);
        tabs->addTab(unreadMailWidget, QStringLiteral("Unread"));
        allMailWidget = new productivity::ui::MailListWidget(
            mailIndexService, mailDeltaSync, graphClient, settings, workDescriptorService, false, tabs);
        tabs->addTab(allMailWidget, QStringLiteral("All mail"));
    } else {
        tabs->addTab(unavailableServicePage(QStringLiteral("Unread mail"), tabs), QStringLiteral("Unread"));
        tabs->addTab(unavailableServicePage(QStringLiteral("All mail"), tabs), QStringLiteral("All mail"));
    }
    auto* workActivityWidget = new productivity::ui::WorkActivityWidget(workDescriptorService, tabs);
    tabs->addTab(workActivityWidget, QStringLiteral("Activity"));
    tabs->addTab(new productivity::ui::SettingsWidget(settings, outlookAuthService, graphClient,
        mailIndexService, mailDeltaSync, workDescriptorService, trayAvailable,
        [&autostartService](bool enabled) { return autostartService.setEnabled(enabled); },
        [&window](bool closeToTray) {
            window.setHideToTray(closeToTray);
        },
        [timerWidget](int seconds) {
            if (timerWidget)
                timerWidget->setDefaultDuration(seconds);
        },
        [&deadlineMonitorControl] {
            if (deadlineMonitorControl)
                deadlineMonitorControl->restart();
        }, [&notificationCoordinator](const QString& title, const QString& message) {
            return notificationCoordinator.notify(title, message);
        }, tabs), QStringLiteral("Settings"));
    window.setCentralWidget(tabs);
    auto* activitySummary = new QLabel(QStringLiteral("No background work"), &window);
    auto* activityProgress = new QProgressBar(&window);
    activityProgress->setFixedWidth(180);
    activityProgress->setMaximumHeight(16);
    activityProgress->hide();
    window.statusBar()->addPermanentWidget(activitySummary, 1);
    window.statusBar()->addPermanentWidget(activityProgress);
    QObject::connect(workActivityWidget, &productivity::ui::WorkActivityWidget::summaryChanged,
                     &window, [activitySummary, activityProgress](const QString& summary, bool indeterminate, int percent) {
        activitySummary->setText(summary);
        activityProgress->setVisible(summary != QLatin1String("No background work"));
        if (indeterminate) {
            activityProgress->setRange(0, 0);
        } else {
            activityProgress->setRange(0, 100);
            activityProgress->setValue(percent);
        }
    });

    QMenu trayMenu;
    QAction* showAction = trayMenu.addAction("Show window");
    QAction* timersAction = trayMenu.addAction("Active timers");
    QAction* todosAction = trayMenu.addAction("TODOs");
    QAction* notesAction = trayMenu.addAction("Notes");
    QAction* settingsAction = trayMenu.addAction("Settings");
    trayMenu.addSeparator();
    QAction* quitAction = trayMenu.addAction("Exit");

    trayIcon.setContextMenu(&trayMenu);

    QObject::connect(showAction, &QAction::triggered, &window, [&window] {
        window.showNormal();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(timersAction, &QAction::triggered, &window, [&window] {
        if (auto* tabWidget = qobject_cast<QTabWidget*>(window.centralWidget()))
            tabWidget->setCurrentIndex(0);
        window.showNormal();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(notesAction, &QAction::triggered, &window, [&window] {
        if (auto* tabWidget = qobject_cast<QTabWidget*>(window.centralWidget()))
            tabWidget->setCurrentIndex(2);
        window.showNormal();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(todosAction, &QAction::triggered, &window, [&window] {
        if (auto* tabWidget = qobject_cast<QTabWidget*>(window.centralWidget()))
            tabWidget->setCurrentIndex(1);
        window.showNormal();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(settingsAction, &QAction::triggered, &window, [&window] {
        if (auto* tabWidget = qobject_cast<QTabWidget*>(window.centralWidget()))
            tabWidget->setCurrentIndex(6);
        window.showNormal();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &window,
                     [&window](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            window.showNormal();
            window.raise();
            window.activateWindow();
        }
    });

    app.setQuitOnLastWindowClosed(true);
    if (trayAvailable) {
        trayIcon.show();
    }

    std::unique_ptr<productivity::services::DeadlineMonitor> deadlineMonitor;
    if (timersReady) {
        deadlineMonitor = std::make_unique<productivity::services::DeadlineMonitor>(timerService, notificationCoordinator, &app);
        deadlineMonitor->start();
        deadlineMonitorControl = deadlineMonitor.get();
    }

    const bool startInBackground = app.arguments().contains("--background") || settings.startMinimized();
    if (!startInBackground || !trayAvailable) {
        window.show();
    }

    const int exitCode = app.exec();
    if (deadlineMonitor)
        deadlineMonitor->stop();
    if (!settings.sync())
        qCWarning(startupLog) << "Could not flush application preferences during shutdown";
    database.close();
    instanceLock.unlock();
    return exitCode;
}
