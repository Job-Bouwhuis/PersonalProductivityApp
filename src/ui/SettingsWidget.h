#pragma once

#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace productivity::services {
class ApplicationSettings;
class OutlookAuthService;
}

namespace productivity::ui {

class SettingsWidget final : public QWidget
{
public:
    SettingsWidget(services::ApplicationSettings& settings,
                   services::OutlookAuthService& outlookAuthService,
                   bool trayAvailable,
                   std::function<bool(bool)> startOnLoginChanged,
                   std::function<void(bool)> closeToTrayChanged,
                   std::function<void(int)> defaultCountdownChanged,
                   std::function<void()> restartServices,
                   QWidget* parent = nullptr);

private:
    void restoreDefaults();

    services::ApplicationSettings& settings_;
    services::OutlookAuthService& outlookAuthService_;
    std::function<bool(bool)> startOnLoginChanged_;
    QCheckBox* startOnLogin_ = nullptr;
    QCheckBox* startMinimized_ = nullptr;
    QCheckBox* closeToTray_ = nullptr;
    QSpinBox* hoursInput_ = nullptr;
    QSpinBox* minutesInput_ = nullptr;
    QSpinBox* secondsInput_ = nullptr;
    QLineEdit* outlookTenantInput_ = nullptr;
    QLineEdit* outlookClientIdInput_ = nullptr;
    QLabel* timerStatus_ = nullptr;
    QLabel* outlookStatus_ = nullptr;
    std::function<void(int)> defaultCountdownChanged_;
    std::function<void()> restartServices_;
};

} // namespace productivity::ui
