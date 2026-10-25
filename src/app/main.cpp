#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QIcon>
#include <QLoggingCategory>
#include <QMenu>
#include <QMainWindow>
#include <QLockFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QLabel>

#include <memory>

#include "logging/ApplicationLogger.h"
#include "notifications/NotificationCoordinator.h"
#include "persistence/ApplicationDatabase.h"
#include "DeadlineMonitor.h"
#include "NotesService.h"
#include "ApplicationSettings.h"
#include "OutlookAuthService.h"
#include "AutostartService.h"
#include "TodoService.h"
#include "TimerService.h"
#include "NotesWidget.h"
#include "SettingsWidget.h"
#include "TodoOverviewWidget.h"
#include "TimerOverviewWidget.h"

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
    productivity::services::OutlookAuthService outlookAuthService(&app);
    productivity::services::AutostartService autostartService;
    productivity::services::DeadlineMonitor* deadlineMonitorControl = nullptr;
    if (!autostartService.setEnabled(settings.startOnLogin()))
        qCWarning(startupLog) << "Could not apply the saved login startup preference";

    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();

    ProductivityWindow window;
    window.setWindowTitle("Personal Productivity Service");
    window.setWindowIcon(QIcon(":/app.svg"));
    window.resize(900, 600);
    window.setHideToTray(trayAvailable && settings.closeToTray());
    auto* tabs = new QTabWidget(&window);
    productivity::ui::TimerOverviewWidget* timerWidget = nullptr;
    if (timersReady) {
        timerWidget = new productivity::ui::TimerOverviewWidget(timerService, tabs);
        timerWidget->setDefaultDuration(settings.defaultCountdownSeconds());
        tabs->addTab(timerWidget, QStringLiteral("Timers"));
    } else {
        tabs->addTab(unavailableServicePage(QStringLiteral("Countdown timers"), tabs), QStringLiteral("Timers"));
    }
    productivity::ui::NotesWidget* notesWidget = nullptr;
    QWidget* notesPage = nullptr;
    if (notesReady) {
        notesWidget = new productivity::ui::NotesWidget(notesService, tabs);
        notesPage = notesWidget;
    } else {
        notesPage = unavailableServicePage(QStringLiteral("Notes"), tabs);
    }
    if (todosReady) {
        tabs->addTab(new productivity::ui::TodoOverviewWidget(todoService, notesService,
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
            }, tabs), QStringLiteral("TODOs"));
    } else {
        tabs->addTab(unavailableServicePage(QStringLiteral("TODO manager"), tabs), QStringLiteral("TODOs"));
    }
    tabs->addTab(notesPage, QStringLiteral("Notes"));
    tabs->addTab(new productivity::ui::SettingsWidget(settings, outlookAuthService, trayAvailable,
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
        }, tabs), QStringLiteral("Settings"));
    window.setCentralWidget(tabs);

    QMenu trayMenu;
    QAction* showAction = trayMenu.addAction("Show window");
    QAction* timersAction = trayMenu.addAction("Active timers");
    QAction* todosAction = trayMenu.addAction("TODOs");
    QAction* notesAction = trayMenu.addAction("Notes");
    QAction* settingsAction = trayMenu.addAction("Settings");
    trayMenu.addSeparator();
    QAction* quitAction = trayMenu.addAction("Exit");

    QSystemTrayIcon trayIcon(QIcon(":/app.svg"));
    trayIcon.setToolTip("Personal Productivity Service");
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
            tabWidget->setCurrentIndex(3);
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

    productivity::infrastructure::NotificationCoordinator notificationCoordinator(&trayIcon);
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
