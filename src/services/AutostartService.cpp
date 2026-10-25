#include "AutostartService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(autostartLog, "application.startup")

namespace productivity::services {
namespace {

QString autostartFilePath()
{
    const QString configDirectory = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDirectory.isEmpty())
        return {};
    return QDir(configDirectory).filePath(QStringLiteral("autostart/personal-productivity-app.desktop"));
}

} // namespace

bool AutostartService::isEnabled() const
{
    const QString path = autostartFilePath();
    return !path.isEmpty() && QFile::exists(path);
}

bool AutostartService::setEnabled(bool enabled) const
{
    const QString path = autostartFilePath();
    if (path.isEmpty()) {
        qCWarning(autostartLog) << "Could not locate the user configuration directory";
        return false;
    }
    if (!enabled) {
        if (!QFile::exists(path) || QFile::remove(path))
            return true;
        qCWarning(autostartLog) << "Could not remove the application autostart entry" << path;
        return false;
    }

    const QFileInfo info(path);
    if (!QDir().mkpath(info.path())) {
        qCWarning(autostartLog) << "Could not create the autostart directory" << info.path();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(autostartLog) << "Could not write the application autostart entry:" << file.errorString();
        return false;
    }
    const QByteArray entry =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Personal Productivity Service\n"
        "Comment=Start the productivity service in the background\n"
        "Exec=personal-productivity-app --background\n"
        "TryExec=personal-productivity-app\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n";
    if (file.write(entry) != entry.size() || !file.commit()) {
        qCWarning(autostartLog) << "Could not save the application autostart entry:" << file.errorString();
        return false;
    }
    return true;
}

} // namespace productivity::services
