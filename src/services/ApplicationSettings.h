#pragma once

#include <QSettings>

namespace productivity::services {

class ApplicationSettings final
{
public:
    bool startMinimized() const;
    bool startOnLogin() const;
    bool closeToTray() const;
    int defaultCountdownSeconds() const;
    QString outlookTenant() const;
    QString outlookClientId() const;
    void setStartMinimized(bool enabled);
    void setStartOnLogin(bool enabled);
    void setCloseToTray(bool enabled);
    void setDefaultCountdownSeconds(int seconds);
    void setOutlookConfiguration(const QString& tenant, const QString& clientId);
    void restoreDefaults();
    bool sync();

private:
    QSettings settings_;
};

} // namespace productivity::services
