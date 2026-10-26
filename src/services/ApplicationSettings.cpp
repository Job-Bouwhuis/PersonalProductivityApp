#include "ApplicationSettings.h"

#include <QSet>

namespace productivity::services {

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

QVariantMap ApplicationSettings::ignoredFolderRules() const
{
    return settings_.value(QStringLiteral("outlook/ignoredFolders")).toMap();
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
        if (trimmed.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        normalized.append(trimmed);
    }
    settings_.setValue(QStringLiteral("outlook/ignoredSenders"), normalized);
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
