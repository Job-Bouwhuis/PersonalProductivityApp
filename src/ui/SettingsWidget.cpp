#include "SettingsWidget.h"

#include "ApplicationSettings.h"
#include "MailIndexService.h"
#include "MailDeltaSyncService.h"
#include "MicrosoftGraphClient.h"
#include "OutlookAuthService.h"
#include "SenderRuleMatcher.h"
#include "WorkDescriptorService.h"

#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequenceEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QQueue>
#include <QPair>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUuid>

#include <memory>
#include <utility>

namespace productivity::ui {

namespace {

struct FolderBrowserState {
    bool loading = false;
    QString currentParentId;
    QString currentParentPath;
    QQueue<QPair<QString, QString>> parentsToLoad;
    quint64 requestId = 0;
    int loadedCount = 0;
};

} // namespace

SettingsWidget::SettingsWidget(services::ApplicationSettings& settings,
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
                               QWidget* parent)
    : QWidget(parent)
    , settings_(settings)
    , outlookAuthService_(outlookAuthService)
    , graphClient_(graphClient)
    , mailIndexService_(mailIndexService)
    , mailDeltaSync_(mailDeltaSync)
    , workService_(workService)
    , startOnLoginChanged_(std::move(startOnLoginChanged))
    , defaultCountdownChanged_(std::move(defaultCountdownChanged))
    , drinkReminderChanged_(std::move(drinkReminderChanged))
    , christmasTimerChanged_(std::move(christmasTimerChanged))
    , saveQuickNoteShortcut_(std::move(saveQuickNoteShortcut))
    , configureQuickNoteShortcut_(std::move(configureQuickNoteShortcut))
    , restartServices_(std::move(restartServices))
    , notify_(std::move(notify))
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
    desktopNotificationsEnabled_ = new QCheckBox(QStringLiteral("Enable desktop notifications"), applicationPage);
    desktopNotificationsEnabled_->setChecked(settings_.desktopNotificationsEnabled());
    applicationLayout->addWidget(desktopNotificationsEnabled_);
    auto* trustedDomainsTitle = new QLabel(QStringLiteral("Trusted email link domains"), applicationPage);
    QFont trustedDomainsFont = trustedDomainsTitle->font();
    trustedDomainsFont.setBold(true);
    trustedDomainsTitle->setFont(trustedDomainsFont);
    applicationLayout->addWidget(trustedDomainsTitle);
    auto* trustedDomainsHelp = new QLabel(
        QStringLiteral("Domains you chose to always trust in an email link prompt. Trust applies to the exact domain; subdomains require separate approval."),
        applicationPage);
    trustedDomainsHelp->setWordWrap(true);
    applicationLayout->addWidget(trustedDomainsHelp);
    auto* trustedDomainsList = new QListWidget(applicationPage);
    trustedDomainsList->setSelectionMode(QAbstractItemView::SingleSelection);
    trustedDomainsList->setMaximumHeight(110);
    applicationLayout->addWidget(trustedDomainsList);
    auto* trustedDomainsActions = new QHBoxLayout;
    auto* removeTrustedDomain = new QPushButton(QStringLiteral("Remove selected domain"), applicationPage);
    auto* trustedDomainsStatus = new QLabel(applicationPage);
    trustedDomainsActions->addWidget(removeTrustedDomain);
    trustedDomainsActions->addWidget(trustedDomainsStatus, 1);
    applicationLayout->addLayout(trustedDomainsActions);
    const auto refreshTrustedDomains = [this, trustedDomainsList] {
        trustedDomainsList->clear();
        trustedDomainsList->addItems(settings_.trustedLinkDomains());
    };
    refreshTrustedDomains();
    connect(removeTrustedDomain, &QPushButton::clicked, this,
            [this, trustedDomainsList, trustedDomainsStatus, refreshTrustedDomains] {
        const auto* selected = trustedDomainsList->currentItem();
        if (!selected) {
            trustedDomainsStatus->setText(QStringLiteral("Select a domain to remove."));
            return;
        }
        const QString workId = workService_.startTask(QStringLiteral("Remove trusted email link domain"));
        QStringList domains = settings_.trustedLinkDomains();
        domains.removeAll(selected->text());
        settings_.setTrustedLinkDomains(domains);
        if (!settings_.sync()) {
            workService_.failTask(workId, QStringLiteral("Could not save trusted domain changes"));
            trustedDomainsStatus->setText(QStringLiteral("Could not save the trusted domain changes."));
            return;
        }
        workService_.completeTask(workId, QStringLiteral("Trusted domain removed"));
        refreshTrustedDomains();
        trustedDomainsStatus->setText(QStringLiteral("Trusted domain removed."));
    });
    applicationLayout->addStretch();
    auto* restartButton = new QPushButton(QStringLiteral("Restart deadline monitor"), applicationPage);
    applicationLayout->addWidget(restartButton, 0, Qt::AlignLeft);
    auto* restartStatus = new QLabel(applicationPage);
    applicationLayout->addWidget(restartStatus);
    auto* quickNoteShortcutStatus = new QLabel(QStringLiteral("Current global shortcut: %1")
        .arg(QKeySequence(settings_.quickNoteShortcut()).toString(QKeySequence::NativeText)), applicationPage);
    applicationLayout->addWidget(quickNoteShortcutStatus);
    quickNoteShortcutInput_ = new QKeySequenceEdit(QKeySequence(settings_.quickNoteShortcut()), applicationPage);
    quickNoteShortcutInput_->setAccessibleName(QStringLiteral("Quick-note global shortcut"));
    quickNoteShortcutInput_->setEnabled(quickNoteShortcutCanBeSetInApp);
    applicationLayout->addWidget(quickNoteShortcutInput_, 0, Qt::AlignLeft);
    auto* saveQuickNoteShortcutButton = new QPushButton(QStringLiteral("Save quick-note shortcut"), applicationPage);
    applicationLayout->addWidget(saveQuickNoteShortcutButton, 0, Qt::AlignLeft);
    saveQuickNoteShortcutButton->setEnabled(quickNoteShortcutCanBeSetInApp);
    auto* configureQuickNoteShortcutButton = new QPushButton(QStringLiteral("Configure in desktop settings"), applicationPage);
    configureQuickNoteShortcutButton->setVisible(quickNoteShortcutCanUsePortalConfig);
    applicationLayout->addWidget(configureQuickNoteShortcutButton, 0, Qt::AlignLeft);
    connect(saveQuickNoteShortcutButton, &QPushButton::clicked, this,
            [this, quickNoteShortcutStatus] {
        const QKeySequence shortcut = quickNoteShortcutInput_->keySequence();
        if (shortcut.isEmpty() || shortcut.count() != 1) {
            quickNoteShortcutStatus->setText(QStringLiteral("Choose a single key combination."));
            return;
        }
        const QString previous = settings_.quickNoteShortcut();
        settings_.setQuickNoteShortcut(shortcut.toString(QKeySequence::PortableText));
        if (!settings_.sync() || !saveQuickNoteShortcut_ || !saveQuickNoteShortcut_(shortcut)) {
            settings_.setQuickNoteShortcut(previous);
            settings_.sync();
            quickNoteShortcutStatus->setText(QStringLiteral("Could not register that global shortcut on this desktop."));
            return;
        }
        quickNoteShortcutStatus->setText(QStringLiteral("Quick-note shortcut saved: %1")
            .arg(shortcut.toString(QKeySequence::NativeText)));
    });
    connect(configureQuickNoteShortcutButton, &QPushButton::clicked, this,
            [this, quickNoteShortcutStatus] {
        if (!configureQuickNoteShortcut_ || !configureQuickNoteShortcut_()) {
            quickNoteShortcutStatus->setText(QStringLiteral("Global shortcut configuration is unavailable on this desktop."));
            return;
        }
        quickNoteShortcutStatus->setText(QStringLiteral("Choose a shortcut in the desktop dialog."));
    });
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
    auto* drinkTitle = new QLabel(QStringLiteral("Drink reminder"), timerPage);
    QFont drinkTitleFont = drinkTitle->font();
    drinkTitleFont.setBold(true);
    drinkTitle->setFont(drinkTitleFont);
    timerLayout->addWidget(drinkTitle);
    drinkReminderEnabledInput_ = new QCheckBox(QStringLiteral("Remind me to drink water"), timerPage);
    drinkReminderEnabledInput_->setChecked(settings_.drinkReminderEnabled());
    timerLayout->addWidget(drinkReminderEnabledInput_);
    auto* drinkIntervalRow = new QHBoxLayout;
    drinkIntervalRow->addWidget(new QLabel(QStringLiteral("Reminder interval"), timerPage));
    drinkReminderIntervalInput_ = new QSpinBox(timerPage);
    drinkReminderIntervalInput_->setRange(1, 1440);
    drinkReminderIntervalInput_->setSuffix(QStringLiteral(" min"));
    drinkReminderIntervalInput_->setValue(settings_.drinkReminderIntervalMinutes());
    drinkIntervalRow->addWidget(drinkReminderIntervalInput_);
    drinkIntervalRow->addStretch();
    timerLayout->addLayout(drinkIntervalRow);
    connect(drinkReminderEnabledInput_, &QCheckBox::toggled,
            drinkReminderIntervalInput_, &QSpinBox::setEnabled);
    drinkReminderIntervalInput_->setEnabled(drinkReminderEnabledInput_->isChecked());
    auto* drinkHelp = new QLabel(QStringLiteral("If a reminder is missed while the app is unavailable, one reminder is sent when it returns; the next one is scheduled a full interval later. No sound is played."), timerPage);
    drinkHelp->setWordWrap(true);
    timerLayout->addWidget(drinkHelp);
    drinkReminderStatus_ = new QLabel(settings_.drinkReminderEnabled()
        ? QStringLiteral("Reminder scheduled every %1 minutes.").arg(settings_.drinkReminderIntervalMinutes())
        : QStringLiteral("Drink reminders are disabled."), timerPage);
    timerLayout->addWidget(drinkReminderStatus_);
    auto* saveDrinkReminder = new QPushButton(QStringLiteral("Save drink reminder"), timerPage);
    timerLayout->addWidget(saveDrinkReminder, 0, Qt::AlignLeft);
    auto* christmasTitle = new QLabel(QStringLiteral("Christmas countdown"), timerPage);
    QFont christmasTitleFont = christmasTitle->font();
    christmasTitleFont.setBold(true);
    christmasTitle->setFont(christmasTitleFont);
    timerLayout->addWidget(christmasTitle);
    christmasTimerEnabledInput_ = new QCheckBox(QStringLiteral("Show an automatic countdown to Christmas"), timerPage);
    christmasTimerEnabledInput_->setChecked(settings_.christmasTimerEnabled());
    quickNoteShortcutInput_->setKeySequence(QKeySequence(settings_.quickNoteShortcut()));
    timerLayout->addWidget(christmasTimerEnabledInput_);
    auto* christmasHelp = new QLabel(QStringLiteral("Adds a yearly Christmas event to the timer overview and sends a festive notification on December 25."), timerPage);
    christmasHelp->setWordWrap(true);
    timerLayout->addWidget(christmasHelp);
    christmasTimerStatus_ = new QLabel(settings_.christmasTimerEnabled()
        ? QStringLiteral("Christmas countdown is enabled.") : QStringLiteral("Christmas countdown is disabled."), timerPage);
    timerLayout->addWidget(christmasTimerStatus_);
    auto* saveChristmasTimer = new QPushButton(QStringLiteral("Save Christmas timer"), timerPage);
    timerLayout->addWidget(saveChristmasTimer, 0, Qt::AlignLeft);
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
        QStringLiteral("Connect with your Microsoft app registration's Application (client) ID. Choose your account type below; "
                       "the app selects the matching sign-in tenant for you. In Entra, register the app for the account types "
                       "you want, add the Mobile and desktop applications platform, and enable public client flows. "
                       "For mailbox access and saved message ID migration, add Microsoft Graph Delegated User.Read, Mail.ReadWrite, and Mail.Send permissions. "
                       "This desktop app signs in as you and does not use a client secret."), outlookPage);
    outlookHelp->setWordWrap(true);
    auto* outlookConfigurationToggle = new QToolButton(outlookPage);
    outlookConfigurationToggle->setText(QStringLiteral("App registration settings"));
    outlookConfigurationToggle->setCheckable(true);
    outlookConfigurationToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    outlookConfigurationToggle->setArrowType(Qt::RightArrow);
    outlookLayout->addWidget(outlookConfigurationToggle, 0, Qt::AlignLeft);
    auto* outlookConfiguration = new QWidget(outlookPage);
    auto* configurationLayout = new QVBoxLayout(outlookConfiguration);
    configurationLayout->setContentsMargins(16, 0, 0, 0);
    configurationLayout->addWidget(outlookHelp);
    outlookForm_ = new QFormLayout;
    outlookAccountTypeInput_ = new QComboBox(outlookPage);
    outlookAccountTypeInput_->addItem(QStringLiteral("Personal and work or school accounts"), QStringLiteral("common"));
    outlookAccountTypeInput_->addItem(QStringLiteral("Personal Microsoft account only"), QStringLiteral("consumers"));
    outlookAccountTypeInput_->addItem(QStringLiteral("Work or school accounts"), QStringLiteral("organizations"));
    outlookAccountTypeInput_->addItem(QStringLiteral("Specific organization (tenant ID or domain)"), QStringLiteral("custom"));
    outlookForm_->addRow(QStringLiteral("Account type"), outlookAccountTypeInput_);
    outlookTenantInput_ = new QLineEdit(settings_.outlookTenant(), outlookPage);
    outlookTenantInput_->setPlaceholderText(QStringLiteral("Tenant ID or verified domain"));
    outlookClientIdInput_ = new QLineEdit(settings_.outlookClientId(), outlookPage);
    outlookClientIdInput_->setPlaceholderText(QStringLiteral("Paste the Application (client) ID GUID"));
    outlookClientIdInput_->setMaxLength(38);
    outlookForm_->addRow(QStringLiteral("Organization tenant"), outlookTenantInput_);
    outlookForm_->addRow(QStringLiteral("Client ID"), outlookClientIdInput_);
    const auto updateTenantVisibility = [this] {
        const bool customTenant = outlookAccountTypeInput_->currentData().toString() == QLatin1String("custom");
        outlookTenantInput_->setVisible(customTenant);
        if (QWidget* label = outlookForm_->labelForField(outlookTenantInput_))
            label->setVisible(customTenant);
    };
    connect(outlookAccountTypeInput_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateTenantVisibility](int) { updateTenantVisibility(); });
    const QString savedTenant = settings_.outlookTenant();
    int accountTypeIndex = outlookAccountTypeInput_->findData(savedTenant);
    if (accountTypeIndex < 0) {
        accountTypeIndex = outlookAccountTypeInput_->findData(QStringLiteral("custom"));
        outlookTenantInput_->setText(savedTenant);
    }
    outlookAccountTypeInput_->setCurrentIndex(accountTypeIndex);
    updateTenantVisibility();
    configurationLayout->addLayout(outlookForm_);
    auto* saveOutlook = new QPushButton(QStringLiteral("Save Outlook configuration"), outlookPage);
    configurationLayout->addWidget(saveOutlook, 0, Qt::AlignLeft);
    outlookConfiguration->setVisible(false);
    outlookLayout->addWidget(outlookConfiguration);
    connect(outlookConfigurationToggle, &QToolButton::toggled, this,
            [outlookConfigurationToggle, outlookConfiguration](bool expanded) {
        outlookConfiguration->setVisible(expanded);
        outlookConfigurationToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    auto* outlookActions = new QHBoxLayout;
    auto* connectOutlook = new QPushButton(QStringLiteral("Connect to Outlook"), outlookPage);
    auto* disconnectOutlook = new QPushButton(QStringLiteral("Disconnect"), outlookPage);
    auto* checkMailboxAccess = new QPushButton(QStringLiteral("Check mailbox access"), outlookPage);
    auto* indexInboxMessages = new QPushButton(QStringLiteral("Index latest mailbox messages"), outlookPage);
    auto* scanAllMail = new QPushButton(QStringLiteral("Scan all mail"), outlookPage);
    auto* syncMailChanges = new QPushButton(QStringLiteral("Sync mail changes"), outlookPage);
    outlookActions->addWidget(connectOutlook);
    outlookActions->addWidget(disconnectOutlook);
    outlookActions->addWidget(checkMailboxAccess);
    outlookActions->addWidget(indexInboxMessages);
    outlookActions->addWidget(scanAllMail);
    outlookActions->addWidget(syncMailChanges);
    outlookActions->addStretch();
    outlookLayout->addLayout(outlookActions);
    auto* mailPollControls = new QHBoxLayout;
    mailPollControls->addWidget(new QLabel(QStringLiteral("Automatic mail scan interval"), outlookPage));
    mailPollIntervalInput_ = new QSpinBox(outlookPage);
    mailPollIntervalInput_->setRange(1, 1440);
    mailPollIntervalInput_->setSuffix(QStringLiteral(" min"));
    mailPollIntervalInput_->setValue(settings_.automaticMailScanIntervalMinutes());
    mailPollControls->addWidget(mailPollIntervalInput_);
    auto* saveMailPollInterval = new QPushButton(QStringLiteral("Save interval"), outlookPage);
    mailPollControls->addWidget(saveMailPollInterval);
    mailPollControls->addStretch();
    outlookLayout->addLayout(mailPollControls);
    mailPollStatus_ = new QLabel(QStringLiteral("Automatic sync runs every %1 minutes while Outlook is connected.")
                                     .arg(settings_.automaticMailScanIntervalMinutes()), outlookPage);
    outlookLayout->addWidget(mailPollStatus_);
    mailNextScanStatus_ = new QLabel(QStringLiteral("Next automatic scan: waiting for Outlook connection."), outlookPage);
    outlookLayout->addWidget(mailNextScanStatus_);
    auto authenticationTaskId = std::make_shared<QString>();
    auto* ignoredSenderToggle = new QToolButton(outlookPage);
    ignoredSenderToggle->setText(QStringLiteral("Ignored sender rules"));
    ignoredSenderToggle->setCheckable(true);
    ignoredSenderToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ignoredSenderToggle->setArrowType(Qt::RightArrow);
    outlookLayout->addWidget(ignoredSenderToggle, 0, Qt::AlignLeft);
    auto* ignoredSenderRules = new QWidget(outlookPage);
    auto* ignoredSenderLayout = new QVBoxLayout(ignoredSenderRules);
    ignoredSenderLayout->setContentsMargins(16, 0, 0, 0);
    auto* ignoredSenderHelp = new QLabel(
        QStringLiteral("Add email addresses or patterns using * anywhere, including after @. Optionally enter a sender display name or username to narrow the match; it can also contain *. Leave it empty to match any sender name at that address or domain. Matching unread messages are marked as read during synchronization, and existing mail is reconciled on the next scheduled scan."), ignoredSenderRules);
    ignoredSenderHelp->setWordWrap(true);
    ignoredSenderLayout->addWidget(ignoredSenderHelp);
    ignoredSenderList_ = new QListWidget(ignoredSenderRules);
    ignoredSenderList_->setMinimumHeight(90);
    ignoredSenderList_->setMaximumHeight(150);
    auto* ignoredSenderList = ignoredSenderList_;
    for (const QString& sender : settings_.ignoredSenderRules()) {
        auto* item = new QListWidgetItem(services::senderRuleDisplayText(sender), ignoredSenderList_);
        item->setData(Qt::UserRole, sender);
    }
    ignoredSenderLayout->addWidget(ignoredSenderList_);
    auto* ignoredSenderControls = new QHBoxLayout;
    auto* ignoredSenderInput = new QLineEdit(ignoredSenderRules);
    ignoredSenderInput->setPlaceholderText(QStringLiteral("*@example.com"));
    auto* ignoredSenderNameInput = new QLineEdit(ignoredSenderRules);
    ignoredSenderNameInput->setPlaceholderText(QStringLiteral("Display name or username (optional)"));
    auto* addIgnoredSender = new QPushButton(QStringLiteral("Add sender"), ignoredSenderRules);
    auto* removeIgnoredSender = new QPushButton(QStringLiteral("Remove selected"), ignoredSenderRules);
    ignoredSenderControls->addWidget(ignoredSenderInput, 1);
    ignoredSenderControls->addWidget(ignoredSenderNameInput, 1);
    ignoredSenderControls->addWidget(addIgnoredSender);
    ignoredSenderControls->addWidget(removeIgnoredSender);
    ignoredSenderLayout->addLayout(ignoredSenderControls);
    auto* ignoredSenderStatus = new QLabel(ignoredSenderRules);
    ignoredSenderLayout->addWidget(ignoredSenderStatus);
    ignoredSenderRules->setVisible(false);
    outlookLayout->addWidget(ignoredSenderRules);
    connect(ignoredSenderToggle, &QToolButton::toggled, this,
            [ignoredSenderToggle, ignoredSenderRules](bool expanded) {
        ignoredSenderRules->setVisible(expanded);
        ignoredSenderToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    const auto persistIgnoredSenders = [this, ignoredSenderList = ignoredSenderList_] {
        QStringList senders;
        for (int i = 0; i < ignoredSenderList->count(); ++i)
            senders.append(ignoredSenderList->item(i)->data(Qt::UserRole).toString());
        settings_.setIgnoredSenderRules(senders);
    };
    connect(addIgnoredSender, &QPushButton::clicked, this,
            [this, ignoredSenderInput, ignoredSenderNameInput, ignoredSenderList, ignoredSenderStatus, persistIgnoredSenders] {
        const QString addressPattern = ignoredSenderInput->text().trimmed();
        const QString senderName = ignoredSenderNameInput->text().trimmed();
        const QString sender = (senderName.isEmpty() ? QString() : senderName + QStringLiteral("||")) + addressPattern;
        if (!services::isValidSenderRule(sender)) {
            ignoredSenderStatus->setText(QStringLiteral("Enter an email address or pattern, such as *@example.com."));
            return;
        }
        for (int i = 0; i < ignoredSenderList->count(); ++i) {
            if (ignoredSenderList->item(i)->data(Qt::UserRole).toString().compare(sender, Qt::CaseInsensitive) == 0) {
                ignoredSenderStatus->setText(QStringLiteral("That sender is already on the list."));
                return;
            }
        }
        const QString workId = workService_.startTask(QStringLiteral("Add ignored sender rule"));
        QString error;
        if (!mailIndexService_.requestAllFolderRescans(error)) {
            workService_.failTask(workId, error);
            ignoredSenderStatus->setText(error);
            return;
        }
        auto* item = new QListWidgetItem(services::senderRuleDisplayText(sender), ignoredSenderList);
        item->setData(Qt::UserRole, sender);
        ignoredSenderInput->clear();
        ignoredSenderNameInput->clear();
        persistIgnoredSenders();
        workService_.completeTask(workId, QStringLiteral("Ignored sender rule saved for the next scheduled mail scan"));
        ignoredSenderStatus->setText(QStringLiteral("Ignored sender saved. Existing mail will be reconciled on the next scheduled scan."));
    });
    connect(removeIgnoredSender, &QPushButton::clicked, this,
            [this, ignoredSenderList, ignoredSenderStatus, persistIgnoredSenders] {
        const QList<QListWidgetItem*> selected = ignoredSenderList->selectedItems();
        if (selected.isEmpty()) {
            ignoredSenderStatus->setText(QStringLiteral("Select a sender to remove."));
            return;
        }
        const QString workId = workService_.startTask(QStringLiteral("Remove ignored sender rule"));
        for (QListWidgetItem* item : selected)
            delete ignoredSenderList->takeItem(ignoredSenderList->row(item));
        persistIgnoredSenders();
        workService_.completeTask(workId, QStringLiteral("Ignored sender rule removed"));
        ignoredSenderStatus->setText(QStringLiteral("Ignored sender removed."));
    });
    auto* importantSenderToggle = new QToolButton(outlookPage);
    importantSenderToggle->setText(QStringLiteral("Important sender rules"));
    importantSenderToggle->setCheckable(true);
    importantSenderToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    importantSenderToggle->setArrowType(Qt::RightArrow);
    outlookLayout->addWidget(importantSenderToggle, 0, Qt::AlignLeft);
    auto* importantSenderRules = new QWidget(outlookPage);
    auto* importantSenderLayout = new QVBoxLayout(importantSenderRules);
    importantSenderLayout->setContentsMargins(16, 0, 0, 0);
    auto* importantSenderHelp = new QLabel(
        QStringLiteral("Add email addresses or patterns using * anywhere, including after @. Optionally enter a sender display name or username to narrow the match; it can also contain *. Leave it empty to match any sender name at that address or domain."), importantSenderRules);
    importantSenderHelp->setWordWrap(true);
    importantSenderLayout->addWidget(importantSenderHelp);
    auto* importantSenderList = new QListWidget(importantSenderRules);
    importantSenderList->setMinimumHeight(70);
    importantSenderList->setMaximumHeight(130);
    for (const QString& sender : settings_.importantSenderRules()) {
        auto* item = new QListWidgetItem(services::senderRuleDisplayText(sender), importantSenderList);
        item->setData(Qt::UserRole, sender);
    }
    importantSenderLayout->addWidget(importantSenderList);
    auto* importantSenderControls = new QHBoxLayout;
    auto* importantSenderInput = new QLineEdit(importantSenderRules);
    importantSenderInput->setPlaceholderText(QStringLiteral("*@example.com"));
    auto* importantSenderNameInput = new QLineEdit(importantSenderRules);
    importantSenderNameInput->setPlaceholderText(QStringLiteral("Display name or username (optional)"));
    auto* addImportantSender = new QPushButton(QStringLiteral("Add sender"), importantSenderRules);
    auto* removeImportantSender = new QPushButton(QStringLiteral("Remove selected"), importantSenderRules);
    importantSenderControls->addWidget(importantSenderInput, 1);
    importantSenderControls->addWidget(importantSenderNameInput, 1);
    importantSenderControls->addWidget(addImportantSender);
    importantSenderControls->addWidget(removeImportantSender);
    importantSenderLayout->addLayout(importantSenderControls);
    auto* importantSenderStatus = new QLabel(importantSenderRules);
    importantSenderLayout->addWidget(importantSenderStatus);
    importantSenderRules->setVisible(false);
    outlookLayout->addWidget(importantSenderRules);
    connect(importantSenderToggle, &QToolButton::toggled, this,
            [importantSenderToggle, importantSenderRules](bool expanded) {
        importantSenderRules->setVisible(expanded);
        importantSenderToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    const auto persistImportantSenders = [this, importantSenderList] {
        QStringList senders;
        for (int i = 0; i < importantSenderList->count(); ++i)
            senders.append(importantSenderList->item(i)->data(Qt::UserRole).toString());
        settings_.setImportantSenderRules(senders);
        return settings_.sync();
    };
    connect(addImportantSender, &QPushButton::clicked, this,
            [this, importantSenderInput, importantSenderNameInput, importantSenderList, importantSenderStatus, persistImportantSenders] {
        const QString addressPattern = importantSenderInput->text().trimmed();
        const QString senderName = importantSenderNameInput->text().trimmed();
        const QString sender = (senderName.isEmpty() ? QString() : senderName + QStringLiteral("||")) + addressPattern;
        if (!services::isValidSenderRule(sender)) {
            importantSenderStatus->setText(QStringLiteral("Enter an email address or pattern, such as *@example.com."));
            return;
        }
        for (int i = 0; i < importantSenderList->count(); ++i) {
            if (importantSenderList->item(i)->data(Qt::UserRole).toString().compare(sender, Qt::CaseInsensitive) == 0) {
                importantSenderStatus->setText(QStringLiteral("That sender is already on the list."));
                return;
            }
        }
        const QString workId = workService_.startTask(QStringLiteral("Add important sender rule"));
        auto* item = new QListWidgetItem(services::senderRuleDisplayText(sender), importantSenderList);
        item->setData(Qt::UserRole, sender);
        if (!persistImportantSenders()) {
            delete importantSenderList->takeItem(importantSenderList->count() - 1);
            workService_.failTask(workId, QStringLiteral("Could not save important sender rule"));
            importantSenderStatus->setText(QStringLiteral("Could not save the sender rule."));
            return;
        }
        importantSenderInput->clear();
        importantSenderNameInput->clear();
        workService_.completeTask(workId, QStringLiteral("Important sender rule saved"));
        importantSenderStatus->setText(QStringLiteral("Important sender saved."));
    });
    connect(removeImportantSender, &QPushButton::clicked, this,
            [this, importantSenderList, importantSenderStatus, persistImportantSenders] {
        const QList<QListWidgetItem*> selected = importantSenderList->selectedItems();
        if (selected.isEmpty()) {
            importantSenderStatus->setText(QStringLiteral("Select a sender to remove."));
            return;
        }
        const QString workId = workService_.startTask(QStringLiteral("Remove important sender rule"));
        for (QListWidgetItem* item : selected)
            delete importantSenderList->takeItem(importantSenderList->row(item));
        if (!persistImportantSenders()) {
            workService_.failTask(workId, QStringLiteral("Could not save important sender changes"));
            importantSenderStatus->setText(QStringLiteral("Could not save the sender changes."));
            return;
        }
        workService_.completeTask(workId, QStringLiteral("Important sender rule removed"));
        importantSenderStatus->setText(QStringLiteral("Important sender removed."));
    });
    auto* ignoredFolderToggle = new QToolButton(outlookPage);
    ignoredFolderToggle->setText(QStringLiteral("Ignored folder rules"));
    ignoredFolderToggle->setCheckable(true);
    ignoredFolderToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ignoredFolderToggle->setArrowType(Qt::RightArrow);
    outlookLayout->addWidget(ignoredFolderToggle, 0, Qt::AlignLeft);
    auto* ignoredFolderRules = new QWidget(outlookPage);
    auto* ignoredFolderLayout = new QVBoxLayout(ignoredFolderRules);
    ignoredFolderLayout->setContentsMargins(16, 0, 0, 0);
    auto* ignoredFolderHelp = new QLabel(
        QStringLiteral("Choose a folder to mark its unread messages as read during synchronization. Rules apply to the selected folder only; adding a rule starts a sync to process existing unread mail."), ignoredFolderRules);
    ignoredFolderHelp->setWordWrap(true);
    ignoredFolderLayout->addWidget(ignoredFolderHelp);
    auto* ignoredFolderSelection = new QHBoxLayout;
    auto* ignoredFolderCombo = new QComboBox(ignoredFolderRules);
    ignoredFolderCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto* loadFoldersButton = new QPushButton(QStringLiteral("Load folders from Outlook"), ignoredFolderRules);
    ignoredFolderSelection->addWidget(ignoredFolderCombo, 1);
    ignoredFolderSelection->addWidget(loadFoldersButton);
    ignoredFolderLayout->addLayout(ignoredFolderSelection);
    auto* ignoredFolderList = new QListWidget(ignoredFolderRules);
    ignoredFolderList->setMinimumHeight(75);
    ignoredFolderList->setMaximumHeight(130);
    const QVariantMap savedIgnoredFolders = settings_.ignoredFolderRules();
    for (auto it = savedIgnoredFolders.cbegin(); it != savedIgnoredFolders.cend(); ++it) {
        auto* item = new QListWidgetItem(it.value().toString(), ignoredFolderList);
        item->setData(Qt::UserRole, it.key());
    }
    ignoredFolderLayout->addWidget(ignoredFolderList);
    auto* ignoredFolderControls = new QHBoxLayout;
    auto* addIgnoredFolder = new QPushButton(QStringLiteral("Ignore selected folder"), ignoredFolderRules);
    auto* removeIgnoredFolder = new QPushButton(QStringLiteral("Remove selected rule"), ignoredFolderRules);
    ignoredFolderControls->addWidget(addIgnoredFolder);
    ignoredFolderControls->addWidget(removeIgnoredFolder);
    ignoredFolderControls->addStretch();
    ignoredFolderLayout->addLayout(ignoredFolderControls);
    auto* ignoredFolderStatus = new QLabel(QStringLiteral("Load folders to choose a rule."), ignoredFolderRules);
    ignoredFolderStatus->setWordWrap(true);
    ignoredFolderLayout->addWidget(ignoredFolderStatus);
    ignoredFolderRules->setVisible(false);
    outlookLayout->addWidget(ignoredFolderRules);
    connect(ignoredFolderToggle, &QToolButton::toggled, this,
            [ignoredFolderToggle, ignoredFolderRules](bool expanded) {
        ignoredFolderRules->setVisible(expanded);
        ignoredFolderToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    const auto persistIgnoredFolders = [this, ignoredFolderList] {
        QVariantMap folders;
        for (int i = 0; i < ignoredFolderList->count(); ++i) {
            const QListWidgetItem* item = ignoredFolderList->item(i);
            folders.insert(item->data(Qt::UserRole).toString(), item->text());
        }
        settings_.setIgnoredFolderRules(folders);
    };
    connect(addIgnoredFolder, &QPushButton::clicked, this,
            [this, ignoredFolderCombo, ignoredFolderList, ignoredFolderStatus, persistIgnoredFolders] {
        const QString folderId = ignoredFolderCombo->currentData().toString();
        if (folderId.isEmpty()) {
            ignoredFolderStatus->setText(QStringLiteral("Load folders and select a folder first."));
            return;
        }
        for (int i = 0; i < ignoredFolderList->count(); ++i) {
            if (ignoredFolderList->item(i)->data(Qt::UserRole).toString() == folderId) {
                ignoredFolderStatus->setText(QStringLiteral("That folder is already on the list."));
                return;
            }
        }
        const QString workId = workService_.startTask(QStringLiteral("Add ignored folder rule"));
        QString error;
        if (!mailIndexService_.requestFolderRescan(folderId, error)) {
            workService_.failTask(workId, error);
            ignoredFolderStatus->setText(error);
            return;
        }
        auto* item = new QListWidgetItem(ignoredFolderCombo->currentText(), ignoredFolderList);
        item->setData(Qt::UserRole, folderId);
        persistIgnoredFolders();
        workService_.completeTask(workId, QStringLiteral("Folder rule saved for the next scheduled mail scan"));
        ignoredFolderStatus->setText(QStringLiteral("Ignored folder saved. Existing unread mail will be reconciled on the next scheduled scan."));
    });
    connect(removeIgnoredFolder, &QPushButton::clicked, this,
            [this, ignoredFolderList, ignoredFolderStatus, persistIgnoredFolders] {
        const QList<QListWidgetItem*> selected = ignoredFolderList->selectedItems();
        if (selected.isEmpty()) {
            ignoredFolderStatus->setText(QStringLiteral("Select a folder rule to remove."));
            return;
        }
        const QString workId = workService_.startTask(QStringLiteral("Remove ignored folder rule"));
        for (QListWidgetItem* item : selected)
            delete ignoredFolderList->takeItem(ignoredFolderList->row(item));
        persistIgnoredFolders();
        workService_.completeTask(workId, QStringLiteral("Ignored folder rule removed"));
        ignoredFolderStatus->setText(QStringLiteral("Ignored folder removed."));
    });
    const auto folderBrowser = std::make_shared<FolderBrowserState>();
    connect(loadFoldersButton, &QPushButton::clicked, this,
            [this, folderBrowser, ignoredFolderCombo, ignoredFolderStatus] {
        if (folderBrowser->loading)
            return;
        folderBrowser->loading = true;
        folderBrowser->currentParentId = QStringLiteral("__root__");
        folderBrowser->currentParentPath.clear();
        folderBrowser->parentsToLoad.clear();
        folderBrowser->loadedCount = 0;
        ignoredFolderCombo->clear();
        ignoredFolderCombo->addItem(QStringLiteral("Select a folder…"), QString());
        ignoredFolderCombo->setCurrentIndex(0);
        ignoredFolderStatus->setText(QStringLiteral("Loading folders from Outlook…"));
        folderBrowser->requestId = graphClient_.listRootMailFolders();
    });
    outlookStatus_ = new QLabel(QStringLiteral("Authentication is not connected yet."), outlookPage);
    outlookStatus_->setWordWrap(true);
    outlookLayout->addWidget(outlookStatus_);
    outlookLayout->addStretch(1);
    serviceTabs->addTab(outlookPage, QStringLiteral("Outlook"));

    layout->addWidget(serviceTabs, 1);
    auto* restoreButton = new QPushButton(QStringLiteral("Restore all defaults"), this);
    layout->addWidget(restoreButton, 0, Qt::AlignLeft);

    connect(startMinimized_, &QCheckBox::toggled, this, [this](bool enabled) {
        const QString workId = workService_.startTask(QStringLiteral("Change startup window preference"));
        settings_.setStartMinimized(enabled);
        workService_.completeTask(workId, QStringLiteral("Startup window preference saved"));
    });
    connect(startOnLogin_, &QCheckBox::toggled, this, [this](bool enabled) {
        const QString workId = workService_.startTask(QStringLiteral("Update login startup setting"));
        if (startOnLoginChanged_ && !startOnLoginChanged_(enabled)) {
            workService_.failTask(workId, QStringLiteral("Could not update the login startup entry"));
            const QSignalBlocker blocker(startOnLogin_);
            startOnLogin_->setChecked(!enabled);
            QMessageBox::warning(this, QStringLiteral("Startup setting not changed"),
                                 QStringLiteral("The login startup entry could not be updated."));
            return;
        }
        settings_.setStartOnLogin(enabled);
        workService_.completeTask(workId, QStringLiteral("Login startup preference saved"));
    });
    connect(closeToTray_, &QCheckBox::toggled, this, [this, closeToTrayChanged](bool enabled) {
        const QString workId = workService_.startTask(QStringLiteral("Change close-to-tray preference"));
        settings_.setCloseToTray(enabled);
        if (closeToTrayChanged)
            closeToTrayChanged(enabled);
        workService_.completeTask(workId, QStringLiteral("Close-to-tray preference saved"));
    });
    connect(desktopNotificationsEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        const bool previous = settings_.desktopNotificationsEnabled();
        const QString workId = workService_.startTask(QStringLiteral("Change desktop notification setting"));
        settings_.setDesktopNotificationsEnabled(enabled);
        if (!settings_.sync()) {
            settings_.setDesktopNotificationsEnabled(previous);
            settings_.sync();
            const QSignalBlocker blocker(desktopNotificationsEnabled_);
            desktopNotificationsEnabled_->setChecked(previous);
            workService_.failTask(workId, QStringLiteral("Could not save desktop notification setting"));
            return;
        }
        workService_.completeTask(workId, enabled
            ? QStringLiteral("Desktop notifications enabled")
            : QStringLiteral("Desktop notifications disabled"));
    });
    connect(restartButton, &QPushButton::clicked, this, [this, restartStatus] {
        const QString workId = workService_.startTask(QStringLiteral("Restart deadline monitor"));
        if (!restartServices_) {
            workService_.failTask(workId, QStringLiteral("No restartable background service is available"));
            restartStatus->setText(QStringLiteral("No restartable background service is available."));
            return;
        }
        restartServices_();
        workService_.completeTask(workId, QStringLiteral("Deadline monitor restarted"));
        restartStatus->setText(QStringLiteral("Deadline monitor restarted."));
    });
    connect(restoreButton, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    connect(saveTimerDefault, &QPushButton::clicked, this, [this] {
        const int seconds = hoursInput_->value() * 3600 + minutesInput_->value() * 60 + secondsInput_->value();
        if (seconds <= 0) {
            timerStatus_->setText(QStringLiteral("Enter a duration greater than zero."));
            return;
        }
        const QString workId = workService_.startTask(QStringLiteral("Save countdown timer default"));
        settings_.setDefaultCountdownSeconds(seconds);
        if (defaultCountdownChanged_)
            defaultCountdownChanged_(seconds);
        timerStatus_->setText(QStringLiteral("Default duration saved."));
        workService_.completeTask(workId, QStringLiteral("Countdown default saved"));
    });
    connect(saveDrinkReminder, &QPushButton::clicked, this, [this] {
        const bool previousEnabled = settings_.drinkReminderEnabled();
        const int previousInterval = settings_.drinkReminderIntervalMinutes();
        const bool enabled = drinkReminderEnabledInput_->isChecked();
        const int intervalMinutes = drinkReminderIntervalInput_->value();
        const QString workId = workService_.startTask(QStringLiteral("Save drink reminder settings"));
        settings_.setDrinkReminderEnabled(enabled);
        settings_.setDrinkReminderIntervalMinutes(intervalMinutes);
        if (!settings_.sync() || (drinkReminderChanged_ && !drinkReminderChanged_(enabled, intervalMinutes))) {
            settings_.setDrinkReminderEnabled(previousEnabled);
            settings_.setDrinkReminderIntervalMinutes(previousInterval);
            settings_.sync();
            workService_.failTask(workId, QStringLiteral("Could not apply drink reminder settings"));
            drinkReminderStatus_->setText(QStringLiteral("Could not save or schedule the drink reminder."));
            return;
        }
        workService_.completeTask(workId, enabled ? QStringLiteral("Drink reminders enabled")
                                                   : QStringLiteral("Drink reminders disabled"));
        drinkReminderStatus_->setText(enabled
            ? QStringLiteral("Reminder scheduled every %1 minutes.").arg(intervalMinutes)
            : QStringLiteral("Drink reminders are disabled."));
    });
    connect(saveChristmasTimer, &QPushButton::clicked, this, [this] {
        const bool previous = settings_.christmasTimerEnabled();
        const bool enabled = christmasTimerEnabledInput_->isChecked();
        const QString workId = workService_.startTask(QStringLiteral("Save Christmas timer settings"));
        settings_.setChristmasTimerEnabled(enabled);
        if (!settings_.sync() || (christmasTimerChanged_ && !christmasTimerChanged_(enabled))) {
            settings_.setChristmasTimerEnabled(previous);
            settings_.sync();
            workService_.failTask(workId, QStringLiteral("Could not apply the Christmas timer settings"));
            christmasTimerStatus_->setText(QStringLiteral("Could not save or schedule the Christmas timer."));
            return;
        }
        workService_.completeTask(workId, enabled ? QStringLiteral("Christmas countdown enabled")
                                                  : QStringLiteral("Christmas countdown disabled"));
        christmasTimerStatus_->setText(enabled ? QStringLiteral("Christmas countdown is enabled.")
                                               : QStringLiteral("Christmas countdown is disabled."));
    });
    connect(saveOutlook, &QPushButton::clicked, this, [this] {
        const QString accountType = outlookAccountTypeInput_->currentData().toString();
        const QString tenant = accountType == QLatin1String("custom")
            ? outlookTenantInput_->text().trimmed() : accountType;
        const QString clientId = outlookClientIdInput_->text().trimmed();
        const bool validTenant = !tenant.isEmpty() && !tenant.contains(QRegularExpression(QStringLiteral("[\\s/?#]")));
        if (!validTenant) {
            outlookStatus_->setText(QStringLiteral("Enter the organization's tenant ID or verified domain."));
            return;
        }
        if (QUuid::fromString(clientId).isNull()) {
            outlookStatus_->setText(QStringLiteral("Paste the Application (client) ID from your app registration's Overview page. Do not use the Object ID."));
            return;
        }
        const QString workId = workService_.startTask(QStringLiteral("Save Outlook configuration"));
        settings_.setOutlookConfiguration(tenant, clientId);
        workService_.completeTask(workId, QStringLiteral("Outlook configuration saved"));
        outlookStatus_->setText(QStringLiteral("Outlook configuration saved. Sign-in is not connected yet."));
    });
    connect(connectOutlook, &QPushButton::clicked, this, [this, outlookConfigurationToggle, authenticationTaskId] {
        const QString accountType = outlookAccountTypeInput_->currentData().toString();
        const QString tenant = accountType == QLatin1String("custom")
            ? outlookTenantInput_->text().trimmed() : accountType;
        const QString clientId = outlookClientIdInput_->text().trimmed();
        if (tenant.isEmpty() || tenant.contains(QRegularExpression(QStringLiteral("[\\s/?#]")))) {
            outlookConfigurationToggle->setChecked(true);
            outlookStatus_->setText(QStringLiteral("Enter the organization's tenant ID or verified domain."));
            return;
        }
        if (QUuid::fromString(clientId).isNull()) {
            outlookConfigurationToggle->setChecked(true);
            outlookStatus_->setText(QStringLiteral("Paste the Application (client) ID from your app registration's Overview page. Do not use the Object ID."));
            return;
        }
        settings_.setOutlookConfiguration(tenant, clientId);
        if (!authenticationTaskId->isEmpty())
            workService_.failTask(*authenticationTaskId, QStringLiteral("A new Outlook sign-in was started."));
        *authenticationTaskId = workService_.startTask(QStringLiteral("Sign in to Outlook"));
        workService_.updateTask(*authenticationTaskId, QStringLiteral("Waiting for Microsoft sign-in…"));
        outlookAuthService_.connectAccount(tenant, clientId);
    });
    connect(disconnectOutlook, &QPushButton::clicked, this, [this, authenticationTaskId] {
        if (authenticationTaskId->isEmpty())
            *authenticationTaskId = workService_.startTask(QStringLiteral("Disconnect Outlook"));
        outlookAuthService_.disconnectAccount();
    });
    auto mailboxAccessRequestId = std::make_shared<quint64>(0);
    auto inboxIndexRequestId = std::make_shared<quint64>(0);
    struct InitialInboxScanState {
        quint64 requestId = 0;
        int indexedCount = 0;
        bool running = false;
        bool manual = false;
        QString workTaskId;
    };
    struct BackgroundMailCheckState {
        quint64 requestId = 0;
        bool running = false;
        QString workTaskId;
    };
    auto initialScan = std::make_shared<InitialInboxScanState>();
    auto backgroundCheck = std::make_shared<BackgroundMailCheckState>();
    mailboxPollTimer_ = new QTimer(this);
    mailboxPollTimer_->setInterval(settings_.automaticMailScanIntervalMinutes() * 60 * 1000);
    auto beginBackgroundCheck = std::make_shared<std::function<void()>>();
    *beginBackgroundCheck = [this, initialScan, backgroundCheck] {
        if (initialScan->running || backgroundCheck->running || mailDeltaSync_.isRunning())
            return;
        backgroundCheck->running = true;
        backgroundCheck->workTaskId = workService_.startTask(QStringLiteral("Check recent Outlook mail"));
        workService_.updateTask(backgroundCheck->workTaskId, QStringLiteral("Checking newest mail pages…"));
        outlookStatus_->setText(QStringLiteral("Checking recent mail…"));
        backgroundCheck->requestId = graphClient_.listMessages(100, backgroundCheck->workTaskId);
    };
    connect(saveMailPollInterval, &QPushButton::clicked, this, [this] {
        const int previousInterval = settings_.automaticMailScanIntervalMinutes();
        const int intervalMinutes = mailPollIntervalInput_->value();
        const QString workId = workService_.startTask(QStringLiteral("Save automatic mail scan interval"));
        settings_.setAutomaticMailScanIntervalMinutes(intervalMinutes);
        if (!settings_.sync()) {
            settings_.setAutomaticMailScanIntervalMinutes(previousInterval);
            settings_.sync();
            workService_.failTask(workId, QStringLiteral("Could not save automatic mail scan interval"));
            mailPollStatus_->setText(QStringLiteral("Could not save the automatic mail scan interval."));
            return;
        }
        mailboxPollTimer_->setInterval(intervalMinutes * 60 * 1000);
        workService_.completeTask(workId, QStringLiteral("Automatic mail scan interval saved"));
        mailPollStatus_->setText(QStringLiteral("Automatic sync runs every %1 minutes while Outlook is connected.")
                                     .arg(intervalMinutes));
    });
    const auto beginMailboxScan = [this, initialScan, backgroundCheck](bool manual) {
        if (initialScan->running || backgroundCheck->running || mailDeltaSync_.isRunning())
            return;
        if (!manual && mailIndexService_.initialMailboxScanComplete())
            return;
        initialScan->running = true;
        initialScan->manual = manual;
        initialScan->indexedCount = 0;
        initialScan->workTaskId = workService_.startTask(
            manual ? QStringLiteral("Scan all Outlook mail") : QStringLiteral("Initial Outlook mail scan"));
        outlookStatus_->setText(manual ? QStringLiteral("Starting a full mailbox scan…")
                                      : QStringLiteral("Starting the initial mailbox scan…"));
        initialScan->requestId = graphClient_.listMessages(100, initialScan->workTaskId);
    };
    auto startInitialScan = std::make_shared<std::function<void()>>();
    *startInitialScan = [this, beginMailboxScan, beginBackgroundCheck] {
        if (mailIndexService_.initialMailboxScanComplete()) {
            mailboxPollTimer_->start();
            (*beginBackgroundCheck)();
        } else {
            beginMailboxScan(false);
        }
    };
    const auto scheduleInitialScanRetry = [this, initialScan](const QString& reason) {
        if (initialScan->manual || mailIndexService_.initialMailboxScanComplete())
            return;
        mailboxPollTimer_->start();
        const int retryMinutes = settings_.automaticMailScanIntervalMinutes();
        outlookStatus_->setText(QStringLiteral("Initial mailbox scan stopped: %1 It will retry automatically in %2 minute(s).")
                                    .arg(reason).arg(retryMinutes));
    };
    connect(mailboxPollTimer_, &QTimer::timeout, this,
            [this, startInitialScan, beginBackgroundCheck] {
        if (mailIndexService_.initialMailboxScanComplete())
            (*beginBackgroundCheck)();
        else
            (*startInitialScan)();
    });
    const auto updateMailScanCountdown = [this, initialScan, backgroundCheck] {
        if (initialScan->running || backgroundCheck->running || mailDeltaSync_.isRunning()) {
            mailNextScanStatus_->setText(QStringLiteral("Mail scan in progress."));
            return;
        }
        if (!mailboxPollTimer_->isActive()) {
            mailNextScanStatus_->setText(QStringLiteral("Next automatic scan: waiting for Outlook connection."));
            return;
        }
        const int totalSeconds = qMax(0, (mailboxPollTimer_->remainingTime() + 999) / 1000);
        const int minutes = totalSeconds / 60;
        const int seconds = totalSeconds % 60;
        mailNextScanStatus_->setText(QStringLiteral("Next automatic scan in %1:%2.")
                                         .arg(minutes, 2, 10, QLatin1Char('0'))
                                         .arg(seconds, 2, 10, QLatin1Char('0')));
    };
    auto* mailCountdownDisplayTimer = new QTimer(this);
    mailCountdownDisplayTimer->setInterval(1000);
    connect(mailCountdownDisplayTimer, &QTimer::timeout, this, updateMailScanCountdown);
    mailCountdownDisplayTimer->start();
    connect(checkMailboxAccess, &QPushButton::clicked, this, [this, mailboxAccessRequestId] {
        outlookStatus_->setText(QStringLiteral("Checking Microsoft Graph mailbox access…"));
        *mailboxAccessRequestId = graphClient_.listMessages(1);
    });
    connect(indexInboxMessages, &QPushButton::clicked, this, [this, inboxIndexRequestId] {
        outlookStatus_->setText(QStringLiteral("Retrieving the latest mailbox messages and updating the local index…"));
        *inboxIndexRequestId = graphClient_.listMessages(100);
    });
    connect(scanAllMail, &QPushButton::clicked, this, [this, beginMailboxScan, initialScan, backgroundCheck] {
        if (initialScan->running || backgroundCheck->running || mailDeltaSync_.isRunning()) {
            outlookStatus_->setText(QStringLiteral("A mailbox scan is already running."));
            return;
        }
        beginMailboxScan(true);
    });
    connect(syncMailChanges, &QPushButton::clicked, this, [this, initialScan, backgroundCheck] {
        if (initialScan->running || backgroundCheck->running || mailDeltaSync_.isRunning()) {
            outlookStatus_->setText(QStringLiteral("A mailbox scan is already running."));
            return;
        }
        mailDeltaSync_.start();
    });
    connect(&graphClient_, &services::MicrosoftGraphClient::requestFinished, this,
            [this, backgroundCheck](quint64 requestId, int httpStatus, const QByteArray& body) {
        if (!backgroundCheck->running || requestId != backgroundCheck->requestId)
            return;
        const auto finishBackgroundCheck = [this, backgroundCheck](bool success, const QString& detail) {
            backgroundCheck->running = false;
            if (success)
                workService_.completeTask(backgroundCheck->workTaskId, detail);
            else
                workService_.failTask(backgroundCheck->workTaskId, detail);
            mailDeltaSync_.start();
        };
        if (httpStatus < 200 || httpStatus >= 300) {
            const QString error = QStringLiteral("Could not check recent mail (HTTP %1). %2 Incremental synchronization will continue.")
                                      .arg(httpStatus).arg(services::MicrosoftGraphClient::httpFailureGuidance(httpStatus));
            outlookStatus_->setText(error);
            finishBackgroundCheck(false, error);
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject response = document.object();
        if (!document.isObject() || !response.value(QStringLiteral("value")).isArray()) {
            const QString error = QStringLiteral("Microsoft returned an unreadable recent mail page.");
            outlookStatus_->setText(error);
            finishBackgroundCheck(false, error);
            return;
        }
        const QJsonArray messages = response.value(QStringLiteral("value")).toArray();
        bool pageKnown = false;
        QString error;
        if (!mailIndexService_.allMessagesKnown(messages, pageKnown, error)) {
            outlookStatus_->setText(error);
            finishBackgroundCheck(false, error);
            return;
        }
        const QString nextLink = response.value(QStringLiteral("@odata.nextLink")).toString();
        if (!pageKnown && !nextLink.isEmpty()) {
            backgroundCheck->requestId = graphClient_.listMailboxMessagesPage(
                nextLink, backgroundCheck->workTaskId);
            workService_.updateTask(backgroundCheck->workTaskId,
                                    QStringLiteral("Recent page had new mail; checking the next page…"));
            outlookStatus_->setText(QStringLiteral("Recent page had new mail; checking the next page…"));
            return;
        }
        const QString detail = pageKnown
            ? QStringLiteral("Stopped at a page already present in the local index")
            : QStringLiteral("Reached the end of recent mail pages");
        finishBackgroundCheck(true, detail);
    });
    connect(&mailDeltaSync_, &services::MailDeltaSyncService::progressChanged,
            outlookStatus_, &QLabel::setText);
    connect(&mailDeltaSync_, &services::MailDeltaSyncService::finished, this,
            [this](int changed, int removed, int untranslatableIds) {
        const int total = mailIndexService_.indexedMessageCount();
        const QString skipped = untranslatableIds > 0
            ? QStringLiteral(" Microsoft could not translate %1 obsolete saved ID(s); those stale local entries were removed.").arg(untranslatableIds)
            : QString();
        outlookStatus_->setText(QStringLiteral("Mail changes synchronized. Updated %1 message(s), removed %2, and %3 message(s) are in the local index.%4")
                                .arg(changed).arg(removed)
                                .arg(total < 0 ? QStringLiteral("an unknown number of") : QString::number(total))
                                .arg(skipped));
    });
    connect(&mailDeltaSync_, &services::MailDeltaSyncService::failed,
            outlookStatus_, &QLabel::setText);
    connect(&mailDeltaSync_, &services::MailDeltaSyncService::newMessagesReceived,
            this, [this](const QJsonArray& messages) {
        if (messages.isEmpty() || !notify_)
            return;
        const QJsonObject first = messages.first().toObject();
        const QJsonObject sender = first.value(QStringLiteral("from")).toObject()
                                      .value(QStringLiteral("emailAddress")).toObject();
        QString senderLabel = sender.value(QStringLiteral("name")).toString().trimmed();
        if (senderLabel.isEmpty())
            senderLabel = sender.value(QStringLiteral("address")).toString().trimmed();
        if (senderLabel.isEmpty())
            senderLabel = QStringLiteral("Unknown sender");
        const QString subject = first.value(QStringLiteral("subject")).toString().trimmed();
        const QString detail = subject.isEmpty() ? senderLabel : QStringLiteral("%1: %2").arg(senderLabel, subject);
        const QString message = messages.size() == 1
            ? detail
            : QStringLiteral("%1 new messages. Latest: %2").arg(messages.size()).arg(detail);
        const QString title = messages.size() == 1
            ? QStringLiteral("New mail") : QStringLiteral("New mail (%1)").arg(messages.size());
        if (!notify_(title, message))
            outlookStatus_->setText(QStringLiteral("New mail arrived, but the desktop notification service is unavailable."));
    });
    const auto requestNextFolderQuery = [this, folderBrowser, ignoredFolderStatus, ignoredFolderCombo] {
        if (folderBrowser->parentsToLoad.isEmpty()) {
            folderBrowser->loading = false;
            ignoredFolderStatus->setText(QStringLiteral("Loaded %1 folder(s). Select one to add a rule.")
                                             .arg(folderBrowser->loadedCount));
            return;
        }
        const auto nextParent = folderBrowser->parentsToLoad.dequeue();
        folderBrowser->currentParentId = nextParent.first;
        folderBrowser->currentParentPath = nextParent.second;
        folderBrowser->requestId = graphClient_.listChildMailFolders(folderBrowser->currentParentId);
    };
    connect(&graphClient_, &services::MicrosoftGraphClient::requestFinished, this,
            [this, folderBrowser, ignoredFolderCombo, ignoredFolderStatus, requestNextFolderQuery]
            (quint64 requestId, int httpStatus, const QByteArray& body) {
        if (!folderBrowser->loading || requestId != folderBrowser->requestId)
            return;
        if (httpStatus != 200) {
            folderBrowser->loading = false;
            ignoredFolderStatus->setText(QStringLiteral("Could not load Outlook folders (HTTP %1). %2")
                                             .arg(httpStatus).arg(services::MicrosoftGraphClient::httpFailureGuidance(httpStatus)));
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject response = document.object();
        if (!document.isObject() || !response.value(QStringLiteral("value")).isArray()) {
            folderBrowser->loading = false;
            ignoredFolderStatus->setText(QStringLiteral("Microsoft returned an unreadable folder list. Try again."));
            return;
        }
        for (const QJsonValue& value : response.value(QStringLiteral("value")).toArray()) {
            const QJsonObject folder = value.toObject();
            const QString id = folder.value(QStringLiteral("id")).toString();
            const QString name = folder.value(QStringLiteral("displayName")).toString().trimmed();
            if (id.isEmpty() || name.isEmpty())
                continue;
            const QString path = folderBrowser->currentParentId == QLatin1String("__root__")
                ? name : folderBrowser->currentParentPath + QLatin1Char('/') + name;
            ignoredFolderCombo->addItem(path, id);
            ++folderBrowser->loadedCount;
            if (folder.value(QStringLiteral("childFolderCount")).toInt() > 0)
                folderBrowser->parentsToLoad.enqueue(qMakePair(id, path));
        }
        const QString nextLink = response.value(QStringLiteral("@odata.nextLink")).toString();
        if (!nextLink.isEmpty()) {
            folderBrowser->requestId = graphClient_.listMailFoldersPage(nextLink);
            return;
        }
        requestNextFolderQuery();
    });
    connect(&graphClient_, &services::MicrosoftGraphClient::requestFinished, this,
            [this, mailboxAccessRequestId, inboxIndexRequestId, initialScan, beginBackgroundCheck,
             scheduleInitialScanRetry]
            (quint64 requestId, int httpStatus, const QByteArray& body) {
        if (initialScan->running && requestId == initialScan->requestId) {
            const QString scanKind = initialScan->manual ? QStringLiteral("mailbox scan")
                                                         : QStringLiteral("initial mailbox scan");
            if (httpStatus != 200) {
                initialScan->running = false;
                const QString error = QStringLiteral("The %1 could not continue (HTTP %2). %3")
                                          .arg(scanKind).arg(httpStatus)
                                          .arg(services::MicrosoftGraphClient::httpFailureGuidance(httpStatus));
                workService_.failTask(initialScan->workTaskId, error);
                if (!initialScan->manual) {
                    scheduleInitialScanRetry(error);
                } else {
                    outlookStatus_->setText(error);
                }
                return;
            }
            const QJsonDocument document = QJsonDocument::fromJson(body);
            const QJsonObject response = document.object();
            if (!document.isObject() || !response.value(QStringLiteral("value")).isArray()) {
                initialScan->running = false;
                const QString error = QStringLiteral("Microsoft returned an unreadable mailbox page. The scan was not marked complete.");
                workService_.failTask(initialScan->workTaskId, error);
                if (!initialScan->manual)
                    scheduleInitialScanRetry(error);
                else
                    outlookStatus_->setText(error);
                return;
            }
            const QJsonArray messages = response.value(QStringLiteral("value")).toArray();
            QString indexError;
            const int indexed = mailIndexService_.indexMessages(messages, indexError);
            if (indexed < 0) {
                initialScan->running = false;
                workService_.failTask(initialScan->workTaskId, indexError);
                if (!initialScan->manual)
                    scheduleInitialScanRetry(indexError);
                else
                    outlookStatus_->setText(indexError);
                return;
            }
            initialScan->indexedCount += indexed;
            const QString nextLink = response.value(QStringLiteral("@odata.nextLink")).toString();
            if (!nextLink.isEmpty()) {
                outlookStatus_->setText(QStringLiteral("%1: indexed %2 messages; retrieving the next page…")
                                        .arg(initialScan->manual ? QStringLiteral("Full mailbox scan")
                                                                 : QStringLiteral("Initial mailbox scan"))
                                        .arg(initialScan->indexedCount));
                const QString detail = QStringLiteral("Indexed %1 messages; retrieving the next page…")
                                           .arg(initialScan->indexedCount);
                workService_.updateTask(initialScan->workTaskId, detail);
                initialScan->requestId = graphClient_.listMailboxMessagesPage(nextLink, initialScan->workTaskId);
                return;
            }
            QString stateError;
            if (!mailIndexService_.markInitialMailboxScanComplete(stateError)) {
                initialScan->running = false;
                workService_.failTask(initialScan->workTaskId, stateError);
                if (!initialScan->manual)
                    scheduleInitialScanRetry(stateError);
                else
                    outlookStatus_->setText(stateError);
                return;
            }
            initialScan->running = false;
            workService_.completeTask(initialScan->workTaskId,
                                      QStringLiteral("Mailbox scan complete; indexed %1 messages").arg(initialScan->indexedCount));
            const int total = mailIndexService_.indexedMessageCount();
            outlookStatus_->setText(QStringLiteral("Mailbox scan complete. Indexed %1 message(s); %2 message(s) are now in the local index.")
                                    .arg(initialScan->indexedCount)
                                    .arg(total < 0 ? QStringLiteral("total unavailable") : QString::number(total)));
            mailboxPollTimer_->start();
            if (!initialScan->manual)
                (*beginBackgroundCheck)();
            else
                mailDeltaSync_.start();
            return;
        }
        if (requestId == *inboxIndexRequestId) {
            if (httpStatus != 200) {
                outlookStatus_->setText(QStringLiteral("Microsoft Graph returned HTTP %1. Check delegated mail permissions and consent.")
                                        .arg(httpStatus));
                return;
            }
            const QJsonObject response = QJsonDocument::fromJson(body).object();
            const QJsonArray messages = response.value(QStringLiteral("value")).toArray();
            QString indexError;
            const int updated = mailIndexService_.indexMessages(messages, indexError);
            if (updated < 0) {
                outlookStatus_->setText(indexError);
                return;
            }
            const int total = mailIndexService_.indexedMessageCount();
            const QString indexedTotal = total < 0 ? QStringLiteral("The local index total is unavailable.")
                                                   : QStringLiteral("%1 message(s) are in the local index.").arg(total);
            outlookStatus_->setText(QStringLiteral("Indexed %1 message(s) from the latest mailbox page. %2")
                                    .arg(updated).arg(indexedTotal));
            mailDeltaSync_.start();
            return;
        }
        if (requestId != *mailboxAccessRequestId)
            return;
        if (httpStatus != 200) {
            outlookStatus_->setText(QStringLiteral("Microsoft Graph returned HTTP %1. Check delegated mail permissions and consent.")
                                    .arg(httpStatus));
            return;
        }
        const QJsonObject response = QJsonDocument::fromJson(body).object();
        const int messagesFound = response.value(QStringLiteral("value")).toArray().size();
        outlookStatus_->setText(QStringLiteral("Microsoft Graph mailbox access is working. Retrieved %1 message header(s).")
                                .arg(messagesFound));
    });
    connect(&graphClient_, &services::MicrosoftGraphClient::requestRetrying,
            this, [this](quint64, int attempt, int delayMs, const QString& reason) {
        outlookStatus_->setText(QStringLiteral("%1. Retrying in %2 second(s) (attempt %3 of 4)…")
                                .arg(reason)
                                .arg(qMax(1, (delayMs + 999) / 1000))
                                .arg(attempt));
    });
    connect(&graphClient_, &services::MicrosoftGraphClient::requestFailed, this,
            [this, mailboxAccessRequestId, inboxIndexRequestId, initialScan, backgroundCheck,
             scheduleInitialScanRetry,
             folderBrowser, ignoredFolderStatus]
            (quint64 requestId, const QString& error) {
        if (backgroundCheck->running && requestId == backgroundCheck->requestId) {
            backgroundCheck->running = false;
            workService_.failTask(backgroundCheck->workTaskId, error);
            outlookStatus_->setText(QStringLiteral("Recent mail check failed; continuing incremental sync: %1").arg(error));
            mailDeltaSync_.start();
        } else if (folderBrowser->loading && requestId == folderBrowser->requestId) {
            folderBrowser->loading = false;
            ignoredFolderStatus->setText(error);
        } else if (requestId == *mailboxAccessRequestId)
            outlookStatus_->setText(error);
        else if (requestId == *inboxIndexRequestId)
            outlookStatus_->setText(error);
        else if (initialScan->running && requestId == initialScan->requestId) {
            initialScan->running = false;
            const QString message = QStringLiteral("The %1 stopped: %2%3")
                                        .arg(initialScan->manual ? QStringLiteral("mailbox scan")
                                                                 : QStringLiteral("initial mailbox scan"),
                                             error,
                                             initialScan->manual ? QString() : QStringLiteral(" It will retry automatically."));
            workService_.failTask(initialScan->workTaskId, message);
            if (!initialScan->manual)
                scheduleInitialScanRetry(message);
            else
                outlookStatus_->setText(message);
        }
    });
    connect(&outlookAuthService_, &services::OutlookAuthService::statusChanged,
            this, [this, startInitialScan, authenticationTaskId](const QString& status) {
        outlookStatus_->setText(status);
        if (!authenticationTaskId->isEmpty()) {
            const QString normalized = status.toCaseFolded();
            if (status.startsWith(QStringLiteral("Outlook account connected.")) ||
                status.startsWith(QStringLiteral("Outlook credentials removed"))) {
                workService_.completeTask(*authenticationTaskId, status);
                authenticationTaskId->clear();
            } else if (normalized.contains(QStringLiteral("failed")) ||
                       normalized.contains(QStringLiteral("declined")) ||
                       normalized.contains(QStringLiteral("expired")) ||
                       normalized.contains(QStringLiteral("unavailable")) ||
                       normalized.startsWith(QStringLiteral("could not")) ||
                       normalized.startsWith(QStringLiteral("microsoft rejected")) ||
                       normalized.startsWith(QStringLiteral("microsoft returned"))) {
                workService_.failTask(*authenticationTaskId, status);
                authenticationTaskId->clear();
            } else {
                workService_.updateTask(*authenticationTaskId, status);
            }
        }
        if (status.startsWith(QStringLiteral("Outlook account connected.")) ||
            status.startsWith(QStringLiteral("Outlook sign-in is saved.")))
            (*startInitialScan)();
        else if (status.startsWith(QStringLiteral("Outlook credentials removed")) ||
                 status.startsWith(QStringLiteral("Outlook sign-in expired or was revoked.")))
            mailboxPollTimer_->stop();
    });
    connect(&outlookAuthService_, &services::OutlookAuthService::deviceAuthorizationRequired,
            this, [this](const QString& userCode, const QUrl& verificationUrl, const QString& instructions) {
        QApplication::clipboard()->setText(userCode);
        const bool opened = QDesktopServices::openUrl(verificationUrl);
        outlookStatus_->setText(QStringLiteral("Sign-in code: %1\n%2").arg(userCode, instructions));
        const QString prompt = opened
            ? QStringLiteral("%1\n\nWe opened the Microsoft sign-in page in your browser and copied this code to your clipboard:\n\n%2")
                  .arg(instructions, userCode)
            : QStringLiteral("%1\n\nThe sign-in page could not be opened automatically. Open this address and enter the code below. The code is also copied to your clipboard.\n\n%2\n\n%3")
                  .arg(instructions, verificationUrl.toString(), userCode);
        QMessageBox box(QMessageBox::Information, QStringLiteral("Sign in to Outlook"), prompt,
                        QMessageBox::Ok, this);
        auto* copyButton = box.addButton(QStringLiteral("Copy code again"), QMessageBox::ActionRole);
        connect(&outlookAuthService_, &services::OutlookAuthService::statusChanged, &box,
                [&box](const QString& status) {
            if (status.startsWith(QStringLiteral("Outlook account connected.")))
                box.accept();
        });
        box.exec();
        if (box.clickedButton() == copyButton)
            QApplication::clipboard()->setText(userCode);
    });
    outlookAuthService_.restoreConnectionStatus(settings_.outlookTenant(), settings_.outlookClientId());
}

void SettingsWidget::refreshIgnoredSenderRules()
{
    if (!ignoredSenderList_)
        return;
    ignoredSenderList_->clear();
    for (const QString& sender : settings_.ignoredSenderRules()) {
        auto* item = new QListWidgetItem(services::senderRuleDisplayText(sender), ignoredSenderList_);
        item->setData(Qt::UserRole, sender);
    }
}

void SettingsWidget::restoreDefaults()
{
    if (QMessageBox::question(this, QStringLiteral("Restore defaults"),
            QStringLiteral("Restore the default application behavior settings?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const QString workId = workService_.startTask(QStringLiteral("Restore application defaults"));
    if (startOnLoginChanged_ && !startOnLoginChanged_(true)) {
        workService_.failTask(workId, QStringLiteral("Could not enable the login startup entry"));
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
    {
        const QSignalBlocker blocker(desktopNotificationsEnabled_);
        desktopNotificationsEnabled_->setChecked(settings_.desktopNotificationsEnabled());
    }
    const int seconds = settings_.defaultCountdownSeconds();
    hoursInput_->setValue(seconds / 3600);
    minutesInput_->setValue((seconds % 3600) / 60);
    secondsInput_->setValue(seconds % 60);
    drinkReminderEnabledInput_->setChecked(settings_.drinkReminderEnabled());
    christmasTimerEnabledInput_->setChecked(settings_.christmasTimerEnabled());
    drinkReminderIntervalInput_->setValue(settings_.drinkReminderIntervalMinutes());
    drinkReminderIntervalInput_->setEnabled(drinkReminderEnabledInput_->isChecked());
    mailPollIntervalInput_->setValue(settings_.automaticMailScanIntervalMinutes());
    mailboxPollTimer_->setInterval(settings_.automaticMailScanIntervalMinutes() * 60 * 1000);
    mailPollStatus_->setText(QStringLiteral("Automatic sync runs every %1 minutes while Outlook is connected.")
                                 .arg(settings_.automaticMailScanIntervalMinutes()));
    const QString tenant = settings_.outlookTenant();
    int accountTypeIndex = outlookAccountTypeInput_->findData(tenant);
    if (accountTypeIndex < 0) {
        accountTypeIndex = outlookAccountTypeInput_->findData(QStringLiteral("custom"));
        outlookTenantInput_->setText(tenant);
    }
    outlookAccountTypeInput_->setCurrentIndex(accountTypeIndex);
    outlookTenantInput_->setVisible(accountTypeIndex == outlookAccountTypeInput_->findData(QStringLiteral("custom")));
    if (QWidget* label = outlookForm_->labelForField(outlookTenantInput_))
        label->setVisible(outlookTenantInput_->isVisible());
    outlookClientIdInput_->clear();
    outlookStatus_->setText(QStringLiteral("Default Outlook configuration restored."));
    if (defaultCountdownChanged_)
        defaultCountdownChanged_(seconds);
    if (drinkReminderChanged_ && !drinkReminderChanged_(settings_.drinkReminderEnabled(),
                                                        settings_.drinkReminderIntervalMinutes())) {
        workService_.failTask(workId, QStringLiteral("Could not restore drink reminder settings"));
        QMessageBox::warning(this, QStringLiteral("Could not restore defaults"),
                             QStringLiteral("The drink reminder could not be updated."));
        return;
    }
    if (christmasTimerChanged_ && !christmasTimerChanged_(settings_.christmasTimerEnabled())) {
        workService_.failTask(workId, QStringLiteral("Could not restore Christmas timer settings"));
        QMessageBox::warning(this, QStringLiteral("Could not restore defaults"),
                             QStringLiteral("The Christmas timer could not be updated."));
        return;
    }
    if (saveQuickNoteShortcut_ && !saveQuickNoteShortcut_(QKeySequence(settings_.quickNoteShortcut()))) {
        workService_.failTask(workId, QStringLiteral("Could not restore the quick-note shortcut"));
        QMessageBox::warning(this, QStringLiteral("Could not restore defaults"),
                             QStringLiteral("The quick-note shortcut could not be updated."));
        return;
    }
    timerStatus_->setText(QStringLiteral("Default settings restored."));
    workService_.completeTask(workId, QStringLiteral("Application defaults restored"));
}

} // namespace productivity::ui
