#pragma once

#include <QSettings>
#include <QStringList>
#include <QVariantMap>

namespace productivity::services {

class ApplicationSettings final
{
public:
    bool startMinimized() const;
    bool startOnLogin() const;
    bool closeToTray() const;
    int defaultCountdownSeconds() const;
    bool drinkReminderEnabled() const;
    int drinkReminderIntervalMinutes() const;
    bool christmasTimerEnabled() const;
    QString quickNoteShortcut() const;
    int automaticMailScanIntervalMinutes() const;
    bool desktopNotificationsEnabled() const;
    QString outlookTenant() const;
    QString outlookClientId() const;
    QStringList ignoredSenderRules() const;
    QStringList importantSenderRules() const;
    QVariantMap ignoredFolderRules() const;
    QStringList trustedLinkDomains() const;
    bool isTrustedLinkDomain(const QString& domain) const;
    bool trustLinkDomain(const QString& domain);
    void setTrustedLinkDomains(const QStringList& domains);
    void setStartMinimized(bool enabled);
    void setStartOnLogin(bool enabled);
    void setCloseToTray(bool enabled);
    void setDefaultCountdownSeconds(int seconds);
    void setDrinkReminderEnabled(bool enabled);
    void setDrinkReminderIntervalMinutes(int minutes);
    void setChristmasTimerEnabled(bool enabled);
    void setQuickNoteShortcut(const QString& shortcut);
    void setAutomaticMailScanIntervalMinutes(int minutes);
    void setDesktopNotificationsEnabled(bool enabled);
    void setOutlookConfiguration(const QString& tenant, const QString& clientId);
    void setIgnoredSenderRules(const QStringList& senders);
    void setImportantSenderRules(const QStringList& senders);
    void setIgnoredFolderRules(const QVariantMap& folderPathsById);
    void restoreDefaults();
    bool sync();

private:
    QSettings settings_;
};

} // namespace productivity::services
