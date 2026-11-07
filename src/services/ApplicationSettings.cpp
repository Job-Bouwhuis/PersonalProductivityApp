#include "ApplicationSettings.h"
#include "SenderRuleMatcher.h"

#include <QSet>
#include <QUrl>

namespace productivity::services {
namespace {

QString normalizedLinkDomain(QString domain)
{
    domain = domain.trimmed();
    while (domain.endsWith(QLatin1Char('.')))
        domain.chop(1);
    if (domain.isEmpty() || domain.contains(QLatin1Char('/')) || domain.contains(QLatin1Char('@')) ||
        domain.contains(QLatin1Char('*')) || domain.contains(QLatin1Char(':')))
        return {};
    const QByteArray ascii = QUrl::toAce(domain);
    return ascii.isEmpty() ? QString() : QString::fromLatin1(ascii).toLower();
}

} // namespace

bool ApplicationSettings::startMinimized() const
{
    return settings_.value(QStringLiteral("application/startMinimized"), false).toBool();
}

bool ApplicationSettings::startOnLogin() const
{
    return settings_.value(QStringLiteral("application/startOnLogin"), true).toBool();
}

bool ApplicationSettings::closeToTray() const
{
    return settings_.value(QStringLiteral("application/closeToTray"), true).toBool();
}

int ApplicationSettings::defaultCountdownSeconds() const
{
    return settings_.value(QStringLiteral("timers/defaultCountdownSeconds"), 300).toInt();
}

bool ApplicationSettings::drinkReminderEnabled() const
{
    return settings_.value(QStringLiteral("timers/drinkReminderEnabled"), false).toBool();
}

int ApplicationSettings::drinkReminderIntervalMinutes() const
{
    return qBound(1, settings_.value(QStringLiteral("timers/drinkReminderIntervalMinutes"), 60).toInt(), 1440);
}

bool ApplicationSettings::christmasTimerEnabled() const
{
    return settings_.value(QStringLiteral("timers/christmasTimerEnabled"), false).toBool();
}

QString ApplicationSettings::quickNoteShortcut() const
{
    return settings_.value(QStringLiteral("application/quickNoteShortcut"),
                           QStringLiteral("Ctrl+Alt+N")).toString();
}

int ApplicationSettings::automaticMailScanIntervalMinutes() const
{
    return qBound(1, settings_.value(QStringLiteral("outlook/automaticMailScanIntervalMinutes"), 1).toInt(), 1440);
}

bool ApplicationSettings::desktopNotificationsEnabled() const
{
    return settings_.value(QStringLiteral("notifications/desktopEnabled"), true).toBool();
}

QString ApplicationSettings::outlookTenant() const
{
    return settings_.value(QStringLiteral("outlook/tenant"), QStringLiteral("common")).toString();
}

QString ApplicationSettings::outlookClientId() const
{
    return settings_.value(QStringLiteral("outlook/clientId")).toString();
}

QStringList ApplicationSettings::ignoredSenderRules() const
{
    return settings_.value(QStringLiteral("outlook/ignoredSenders")).toStringList();
}

QStringList ApplicationSettings::importantSenderRules() const
{
    return settings_.value(QStringLiteral("outlook/importantSenders")).toStringList();
}

QVariantMap ApplicationSettings::ignoredFolderRules() const
{
    return settings_.value(QStringLiteral("outlook/ignoredFolders")).toMap();
}

QStringList ApplicationSettings::trustedLinkDomains() const
{
    QStringList normalized;
    QSet<QString> seen;
    const QStringList saved = settings_.value(QStringLiteral("mail/trustedLinkDomains")).toStringList();
    for (const QString& domain : saved) {
        const QString key = normalizedLinkDomain(domain);
        if (!key.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            normalized.append(key);
        }
    }
    return normalized;
}

bool ApplicationSettings::isTrustedLinkDomain(const QString& domain) const
{
    const QString normalized = normalizedLinkDomain(domain);
    return !normalized.isEmpty() && trustedLinkDomains().contains(normalized);
}

bool ApplicationSettings::trustLinkDomain(const QString& domain)
{
    const QString normalized = normalizedLinkDomain(domain);
    if (normalized.isEmpty())
        return false;
    QStringList domains = trustedLinkDomains();
    if (!domains.contains(normalized))
        domains.append(normalized);
    setTrustedLinkDomains(domains);
    return sync();
}

void ApplicationSettings::setTrustedLinkDomains(const QStringList& domains)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& domain : domains) {
        const QString key = normalizedLinkDomain(domain);
        if (!key.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            normalized.append(key);
        }
    }
    settings_.setValue(QStringLiteral("mail/trustedLinkDomains"), normalized);
}

void ApplicationSettings::setStartMinimized(bool enabled)
{
    settings_.setValue(QStringLiteral("application/startMinimized"), enabled);
}

void ApplicationSettings::setStartOnLogin(bool enabled)
{
    settings_.setValue(QStringLiteral("application/startOnLogin"), enabled);
}

void ApplicationSettings::setCloseToTray(bool enabled)
{
    settings_.setValue(QStringLiteral("application/closeToTray"), enabled);
}

void ApplicationSettings::setDefaultCountdownSeconds(int seconds)
{
    if (seconds > 0 && seconds <= 3599999)
        settings_.setValue(QStringLiteral("timers/defaultCountdownSeconds"), seconds);
}

void ApplicationSettings::setDrinkReminderEnabled(bool enabled)
{
    settings_.setValue(QStringLiteral("timers/drinkReminderEnabled"), enabled);
}

void ApplicationSettings::setDrinkReminderIntervalMinutes(int minutes)
{
    if (minutes >= 1 && minutes <= 1440)
        settings_.setValue(QStringLiteral("timers/drinkReminderIntervalMinutes"), minutes);
}

void ApplicationSettings::setChristmasTimerEnabled(bool enabled)
{
    settings_.setValue(QStringLiteral("timers/christmasTimerEnabled"), enabled);
}

void ApplicationSettings::setQuickNoteShortcut(const QString& shortcut)
{
    settings_.setValue(QStringLiteral("application/quickNoteShortcut"), shortcut);
}

void ApplicationSettings::setAutomaticMailScanIntervalMinutes(int minutes)
{
    if (minutes >= 1 && minutes <= 1440)
        settings_.setValue(QStringLiteral("outlook/automaticMailScanIntervalMinutes"), minutes);
}

void ApplicationSettings::setDesktopNotificationsEnabled(bool enabled)
{
    settings_.setValue(QStringLiteral("notifications/desktopEnabled"), enabled);
}

void ApplicationSettings::setOutlookConfiguration(const QString& tenant, const QString& clientId)
{
    settings_.setValue(QStringLiteral("outlook/tenant"), tenant.trimmed());
    settings_.setValue(QStringLiteral("outlook/clientId"), clientId.trimmed());
}

void ApplicationSettings::setIgnoredSenderRules(const QStringList& senders)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& sender : senders) {
        const QString trimmed = sender.trimmed();
        const QString key = trimmed.toCaseFolded();
        if (!isValidSenderRule(trimmed) || seen.contains(key))
            continue;
        seen.insert(key);
        normalized.append(trimmed);
    }
    settings_.setValue(QStringLiteral("outlook/ignoredSenders"), normalized);
}

void ApplicationSettings::setImportantSenderRules(const QStringList& senders)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& sender : senders) {
        const QString trimmed = sender.trimmed();
        const QString key = trimmed.toCaseFolded();
        if (!isValidSenderRule(trimmed) || seen.contains(key))
            continue;
        seen.insert(key);
        normalized.append(trimmed);
    }
    settings_.setValue(QStringLiteral("outlook/importantSenders"), normalized);
}

void ApplicationSettings::setIgnoredFolderRules(const QVariantMap& folderPathsById)
{
    QVariantMap normalized;
    for (auto it = folderPathsById.cbegin(); it != folderPathsById.cend(); ++it) {
        const QString id = it.key().trimmed();
        const QString path = it.value().toString().trimmed();
        if (!id.isEmpty() && !path.isEmpty())
            normalized.insert(id, path);
    }
    settings_.setValue(QStringLiteral("outlook/ignoredFolders"), normalized);
}

void ApplicationSettings::restoreDefaults()
{
    settings_.clear();
}

bool ApplicationSettings::sync()
{
    settings_.sync();
    return settings_.status() == QSettings::NoError;
}

} // namespace productivity::services
