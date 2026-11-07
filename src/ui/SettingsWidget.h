#pragma once

#include <QWidget>
#include <QKeySequence>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QFormLayout;
class QListWidget;
class QSpinBox;
class QTimer;
class QKeySequenceEdit;

namespace productivity::services {
class ApplicationSettings;
class OutlookAuthService;
class MicrosoftGraphClient;
class MailIndexService;
class MailDeltaSyncService;
class WorkDescriptorService;
}

namespace productivity::ui {

class SettingsWidget final : public QWidget
{
public:
    SettingsWidget(services::ApplicationSettings& settings,
                   services::OutlookAuthService& outlookAuthService,
                   services::MicrosoftGraphClient& graphClient,
                   services::MailIndexService& mailIndexService,
                   services::MailDeltaSyncService& mailDeltaSync,
                   services::WorkDescriptorService& workService,
                   bool trayAvailable,
                   std::function<bool(bool)> startOnLoginChanged,
                   std::function<void(bool)> closeToTrayChanged,
                   std::function<void(int)> defaultCountdownChanged,
                   std::function<bool(bool, int)> drinkReminderChanged,
                   std::function<bool(bool)> christmasTimerChanged,
                   bool quickNoteShortcutCanBeSetInApp,
                   bool quickNoteShortcutCanUsePortalConfig,
                   std::function<bool(const QKeySequence&)> saveQuickNoteShortcut,
                   std::function<bool()> configureQuickNoteShortcut,
                   std::function<void()> restartServices,
                   std::function<bool(const QString&, const QString&)> notify,
                   QWidget* parent = nullptr);
    void refreshIgnoredSenderRules();

private:
    void restoreDefaults();

    services::ApplicationSettings& settings_;
    services::OutlookAuthService& outlookAuthService_;
    services::MicrosoftGraphClient& graphClient_;
    services::MailIndexService& mailIndexService_;
    services::MailDeltaSyncService& mailDeltaSync_;
    services::WorkDescriptorService& workService_;
    std::function<bool(bool)> startOnLoginChanged_;
    QCheckBox* startOnLogin_ = nullptr;
    QCheckBox* startMinimized_ = nullptr;
    QCheckBox* closeToTray_ = nullptr;
    QCheckBox* desktopNotificationsEnabled_ = nullptr;
    QSpinBox* hoursInput_ = nullptr;
    QSpinBox* minutesInput_ = nullptr;
    QSpinBox* secondsInput_ = nullptr;
    QSpinBox* drinkReminderIntervalInput_ = nullptr;
    QCheckBox* drinkReminderEnabledInput_ = nullptr;
    QCheckBox* christmasTimerEnabledInput_ = nullptr;
    QKeySequenceEdit* quickNoteShortcutInput_ = nullptr;
    QSpinBox* mailPollIntervalInput_ = nullptr;
    QComboBox* outlookAccountTypeInput_ = nullptr;
    QLineEdit* outlookTenantInput_ = nullptr;
    QLineEdit* outlookClientIdInput_ = nullptr;
    QFormLayout* outlookForm_ = nullptr;
    QLabel* timerStatus_ = nullptr;
    QLabel* drinkReminderStatus_ = nullptr;
    QLabel* christmasTimerStatus_ = nullptr;
    QLabel* mailPollStatus_ = nullptr;
    QLabel* mailNextScanStatus_ = nullptr;
    QLabel* outlookStatus_ = nullptr;
    QListWidget* ignoredSenderList_ = nullptr;
    std::function<void(int)> defaultCountdownChanged_;
    std::function<bool(bool, int)> drinkReminderChanged_;
    std::function<bool(bool)> christmasTimerChanged_;
    std::function<bool(const QKeySequence&)> saveQuickNoteShortcut_;
    std::function<bool()> configureQuickNoteShortcut_;
    QTimer* mailboxPollTimer_ = nullptr;
    std::function<void()> restartServices_;
    std::function<bool(const QString&, const QString&)> notify_;
};

} // namespace productivity::ui
