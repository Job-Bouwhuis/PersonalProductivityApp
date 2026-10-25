#include "ApplicationSettings.h"

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
