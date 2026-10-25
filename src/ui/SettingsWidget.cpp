#include "SettingsWidget.h"

#include "ApplicationSettings.h"
#include "OutlookAuthService.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QUuid>

#include <utility>

namespace productivity::ui {

SettingsWidget::SettingsWidget(services::ApplicationSettings& settings,
                               services::OutlookAuthService& outlookAuthService,
                               bool trayAvailable,
                               std::function<bool(bool)> startOnLoginChanged,
                               std::function<void(bool)> closeToTrayChanged,
                               std::function<void(int)> defaultCountdownChanged,
                               std::function<void()> restartServices,
                               QWidget* parent)
    : QWidget(parent)
    , settings_(settings)
    , outlookAuthService_(outlookAuthService)
    , startOnLoginChanged_(std::move(startOnLoginChanged))
    , defaultCountdownChanged_(std::move(defaultCountdownChanged))
    , restartServices_(std::move(restartServices))
{
    auto* layout = new QVBoxLayout(this);
    auto* serviceTabs = new QTabWidget(this);
    auto* applicationPage = new QWidget(serviceTabs);
    auto* applicationLayout = new QVBoxLayout(applicationPage);
    auto* applicationTitle = new QLabel(QStringLiteral("Application behavior"), applicationPage);
    QFont titleFont = applicationTitle->font();
    titleFont.setBold(true);
    applicationTitle->setFont(titleFont);
    applicationLayout->addWidget(applicationTitle);

    startOnLogin_ = new QCheckBox(QStringLiteral("Start automatically when I log in"), applicationPage);
    startOnLogin_->setChecked(settings_.startOnLogin());
    applicationLayout->addWidget(startOnLogin_);

    startMinimized_ = new QCheckBox(QStringLiteral("Start with the main window hidden"), applicationPage);
    startMinimized_->setToolTip(QStringLiteral("Applies the next time the application starts."));
    startMinimized_->setChecked(settings_.startMinimized());
    applicationLayout->addWidget(startMinimized_);

    closeToTray_ = new QCheckBox(QStringLiteral("Hide to the system tray when the window is closed"), applicationPage);
    closeToTray_->setChecked(settings_.closeToTray());
    closeToTray_->setEnabled(trayAvailable);
    if (!trayAvailable)
        closeToTray_->setToolTip(QStringLiteral("No system tray is available in this desktop session."));
    applicationLayout->addWidget(closeToTray_);
    if (!trayAvailable) {
        auto* unavailable = new QLabel(QStringLiteral("Close-to-tray behavior requires a system tray."), applicationPage);
        unavailable->setWordWrap(true);
        applicationLayout->addWidget(unavailable);
    }
    applicationLayout->addStretch();
    auto* restartButton = new QPushButton(QStringLiteral("Restart deadline monitor"), applicationPage);
    applicationLayout->addWidget(restartButton, 0, Qt::AlignLeft);
    auto* restartStatus = new QLabel(applicationPage);
    applicationLayout->addWidget(restartStatus);
    serviceTabs->addTab(applicationPage, QStringLiteral("Application"));

    auto* timerPage = new QWidget(serviceTabs);
    auto* timerLayout = new QVBoxLayout(timerPage);
    auto* timerTitle = new QLabel(QStringLiteral("Countdown timer defaults"), timerPage);
    QFont timerTitleFont = timerTitle->font();
    timerTitleFont.setBold(true);
    timerTitle->setFont(timerTitleFont);
    timerLayout->addWidget(timerTitle);
    auto* timerDuration = new QHBoxLayout;
    const int defaultSeconds = settings_.defaultCountdownSeconds();
    hoursInput_ = new QSpinBox(timerPage);
    hoursInput_->setRange(0, 999);
    hoursInput_->setSuffix(QStringLiteral(" h"));
    hoursInput_->setValue(defaultSeconds / 3600);
    minutesInput_ = new QSpinBox(timerPage);
    minutesInput_->setRange(0, 59);
    minutesInput_->setSuffix(QStringLiteral(" min"));
    minutesInput_->setValue((defaultSeconds % 3600) / 60);
    secondsInput_ = new QSpinBox(timerPage);
    secondsInput_->setRange(0, 59);
    secondsInput_->setSuffix(QStringLiteral(" s"));
    secondsInput_->setValue(defaultSeconds % 60);
    timerDuration->addWidget(hoursInput_);
    timerDuration->addWidget(minutesInput_);
    timerDuration->addWidget(secondsInput_);
    timerDuration->addStretch();
    timerLayout->addLayout(timerDuration);
    timerStatus_ = new QLabel(QStringLiteral("Applies to new countdowns after saving."), timerPage);
    timerLayout->addWidget(timerStatus_);
    auto* saveTimerDefault = new QPushButton(QStringLiteral("Save timer default"), timerPage);
    timerLayout->addWidget(saveTimerDefault, 0, Qt::AlignLeft);
    timerLayout->addStretch();
    serviceTabs->addTab(timerPage, QStringLiteral("Countdown timers"));

    auto* outlookPage = new QWidget(serviceTabs);
    auto* outlookLayout = new QVBoxLayout(outlookPage);
    auto* outlookTitle = new QLabel(QStringLiteral("Outlook account connection"), outlookPage);
    QFont outlookTitleFont = outlookTitle->font();
    outlookTitleFont.setBold(true);
    outlookTitle->setFont(outlookTitleFont);
    outlookLayout->addWidget(outlookTitle);
    auto* outlookHelp = new QLabel(
        QStringLiteral("Enter the public-client application ID and tenant from your Microsoft app registration. "
                       "A client secret is not used by this desktop app."), outlookPage);
    outlookHelp->setWordWrap(true);
    outlookLayout->addWidget(outlookHelp);
    auto* outlookForm = new QFormLayout;
    outlookTenantInput_ = new QLineEdit(settings_.outlookTenant(), outlookPage);
    outlookTenantInput_->setPlaceholderText(QStringLiteral("common, organizations, consumers, or tenant ID"));
    outlookClientIdInput_ = new QLineEdit(settings_.outlookClientId(), outlookPage);
    outlookClientIdInput_->setPlaceholderText(QStringLiteral("Application (client) ID"));
    outlookForm->addRow(QStringLiteral("Tenant"), outlookTenantInput_);
    outlookForm->addRow(QStringLiteral("Client ID"), outlookClientIdInput_);
    outlookLayout->addLayout(outlookForm);
    auto* saveOutlook = new QPushButton(QStringLiteral("Save Outlook configuration"), outlookPage);
    outlookLayout->addWidget(saveOutlook, 0, Qt::AlignLeft);
    auto* outlookActions = new QHBoxLayout;
    auto* connectOutlook = new QPushButton(QStringLiteral("Connect to Outlook"), outlookPage);
    auto* disconnectOutlook = new QPushButton(QStringLiteral("Disconnect"), outlookPage);
    outlookActions->addWidget(connectOutlook);
    outlookActions->addWidget(disconnectOutlook);
    outlookActions->addStretch();
    outlookLayout->addLayout(outlookActions);
    outlookStatus_ = new QLabel(QStringLiteral("Authentication is not connected yet."), outlookPage);
    outlookStatus_->setWordWrap(true);
    outlookLayout->addWidget(outlookStatus_);
    outlookLayout->addStretch();
    serviceTabs->addTab(outlookPage, QStringLiteral("Outlook"));

    layout->addWidget(serviceTabs, 1);
    auto* restoreButton = new QPushButton(QStringLiteral("Restore all defaults"), this);
    layout->addWidget(restoreButton, 0, Qt::AlignLeft);

    connect(startMinimized_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_.setStartMinimized(enabled);
    });
    connect(startOnLogin_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (startOnLoginChanged_ && !startOnLoginChanged_(enabled)) {
            const QSignalBlocker blocker(startOnLogin_);
            startOnLogin_->setChecked(!enabled);
            QMessageBox::warning(this, QStringLiteral("Startup setting not changed"),
                                 QStringLiteral("The login startup entry could not be updated."));
            return;
        }
        settings_.setStartOnLogin(enabled);
    });
    connect(closeToTray_, &QCheckBox::toggled, this, [this, closeToTrayChanged](bool enabled) {
        settings_.setCloseToTray(enabled);
        if (closeToTrayChanged)
            closeToTrayChanged(enabled);
    });
    connect(restartButton, &QPushButton::clicked, this, [this, restartStatus] {
        if (!restartServices_) {
            restartStatus->setText(QStringLiteral("No restartable background service is available."));
            return;
        }
        restartServices_();
        restartStatus->setText(QStringLiteral("Deadline monitor restarted."));
    });
    connect(restoreButton, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    connect(saveTimerDefault, &QPushButton::clicked, this, [this] {
        const int seconds = hoursInput_->value() * 3600 + minutesInput_->value() * 60 + secondsInput_->value();
        if (seconds <= 0) {
            timerStatus_->setText(QStringLiteral("Enter a duration greater than zero."));
            return;
        }
        settings_.setDefaultCountdownSeconds(seconds);
        if (defaultCountdownChanged_)
            defaultCountdownChanged_(seconds);
        timerStatus_->setText(QStringLiteral("Default duration saved."));
    });
    connect(saveOutlook, &QPushButton::clicked, this, [this] {
        const QString tenant = outlookTenantInput_->text().trimmed();
        const QString clientId = outlookClientIdInput_->text().trimmed();
        const bool validTenant = !tenant.isEmpty() && !tenant.contains(QRegularExpression(QStringLiteral("[\\s/?#]")));
        if (!validTenant || QUuid::fromString(clientId).isNull()) {
            outlookStatus_->setText(QStringLiteral("Enter a valid tenant and application client ID."));
            return;
        }
        settings_.setOutlookConfiguration(tenant, clientId);
        outlookStatus_->setText(QStringLiteral("Outlook configuration saved. Sign-in is not connected yet."));
    });
    connect(connectOutlook, &QPushButton::clicked, this, [this] {
        const QString tenant = outlookTenantInput_->text().trimmed();
        const QString clientId = outlookClientIdInput_->text().trimmed();
        if (tenant.isEmpty() || QUuid::fromString(clientId).isNull()) {
            outlookStatus_->setText(QStringLiteral("Save a valid tenant and client ID before connecting."));
            return;
        }
        settings_.setOutlookConfiguration(tenant, clientId);
        outlookAuthService_.connectAccount(tenant, clientId);
    });
    connect(disconnectOutlook, &QPushButton::clicked, this, [this] {
        outlookAuthService_.disconnectAccount();
    });
    connect(&outlookAuthService_, &services::OutlookAuthService::statusChanged,
            outlookStatus_, &QLabel::setText);
    connect(&outlookAuthService_, &services::OutlookAuthService::deviceAuthorizationRequired,
            this, [this](const QString& userCode, const QUrl& verificationUrl, const QString& instructions) {
        outlookStatus_->setText(QStringLiteral("Sign-in code: %1\n%2").arg(userCode, instructions));
        const QString prompt = QStringLiteral("%1\n\nSign-in code: %2\n\nOpen %3 to continue.")
            .arg(instructions, userCode, verificationUrl.toString());
        QMessageBox box(QMessageBox::Information, QStringLiteral("Sign in to Outlook"), prompt,
                        QMessageBox::Ok, this);
        auto* openButton = box.addButton(QStringLiteral("Open sign-in page"), QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == openButton)
            QDesktopServices::openUrl(verificationUrl);
    });
}

void SettingsWidget::restoreDefaults()
{
    if (QMessageBox::question(this, QStringLiteral("Restore defaults"),
            QStringLiteral("Restore the default application behavior settings?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    if (startOnLoginChanged_ && !startOnLoginChanged_(true)) {
        QMessageBox::warning(this, QStringLiteral("Could not restore defaults"),
                             QStringLiteral("The login startup entry could not be enabled."));
        return;
    }
    settings_.restoreDefaults();
    {
        const QSignalBlocker blocker(startOnLogin_);
        startOnLogin_->setChecked(settings_.startOnLogin());
    }
    startMinimized_->setChecked(settings_.startMinimized());
    closeToTray_->setChecked(settings_.closeToTray());
    const int seconds = settings_.defaultCountdownSeconds();
    hoursInput_->setValue(seconds / 3600);
    minutesInput_->setValue((seconds % 3600) / 60);
    secondsInput_->setValue(seconds % 60);
    outlookTenantInput_->setText(settings_.outlookTenant());
    outlookClientIdInput_->clear();
    outlookStatus_->setText(QStringLiteral("Default Outlook configuration restored."));
    if (defaultCountdownChanged_)
        defaultCountdownChanged_(seconds);
    timerStatus_->setText(QStringLiteral("Default settings restored."));
}

} // namespace productivity::ui
