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
    QString outlookTenant() const;
    QString outlookClientId() const;
    QStringList ignoredSenderRules() const;
    QVariantMap ignoredFolderRules() const;
    void setStartMinimized(bool enabled);
    void setStartOnLogin(bool enabled);
    void setCloseToTray(bool enabled);
    void setDefaultCountdownSeconds(int seconds);
    void setOutlookConfiguration(const QString& tenant, const QString& clientId);
    void setIgnoredSenderRules(const QStringList& senders);
    void setIgnoredFolderRules(const QVariantMap& folderPathsById);
    void restoreDefaults();
    bool sync();

private:
    QSettings settings_;
};

} // namespace productivity::services
