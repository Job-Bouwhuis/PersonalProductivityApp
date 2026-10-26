#pragma once

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QFormLayout;
class QSpinBox;

namespace productivity::services {
class ApplicationSettings;
class OutlookAuthService;
class MicrosoftGraphClient;
class MailIndexService;
class MailDeltaSyncService;
}

namespace productivity::ui {

class SettingsWidget final : public QWidget
{
public:
    SettingsWidget(services::ApplicationSettings& settings,
                   services::OutlookAuthService& outlookAuthService,
                   services::MicrosoftGraphClient& graphClient,
                   services::MailIndexService& mailIndexService,
                   bool trayAvailable,
                   std::function<bool(bool)> startOnLoginChanged,
                   std::function<void(bool)> closeToTrayChanged,
                   std::function<void(int)> defaultCountdownChanged,
                   std::function<void()> restartServices,
                   std::function<bool(const QString&, const QString&)> notify,
                   QWidget* parent = nullptr);

private:
    void restoreDefaults();

    services::ApplicationSettings& settings_;
    services::OutlookAuthService& outlookAuthService_;
    services::MicrosoftGraphClient& graphClient_;
    services::MailIndexService& mailIndexService_;
    services::MailDeltaSyncService* mailDeltaSync_ = nullptr;
    std::function<bool(bool)> startOnLoginChanged_;
    QCheckBox* startOnLogin_ = nullptr;
    QCheckBox* startMinimized_ = nullptr;
    QCheckBox* closeToTray_ = nullptr;
    QSpinBox* hoursInput_ = nullptr;
    QSpinBox* minutesInput_ = nullptr;
    QSpinBox* secondsInput_ = nullptr;
    QComboBox* outlookAccountTypeInput_ = nullptr;
    QLineEdit* outlookTenantInput_ = nullptr;
    QLineEdit* outlookClientIdInput_ = nullptr;
    QFormLayout* outlookForm_ = nullptr;
    QLabel* timerStatus_ = nullptr;
    QLabel* outlookStatus_ = nullptr;
    std::function<void(int)> defaultCountdownChanged_;
    std::function<void()> restartServices_;
    std::function<bool(const QString&, const QString&)> notify_;
};

} // namespace productivity::ui
