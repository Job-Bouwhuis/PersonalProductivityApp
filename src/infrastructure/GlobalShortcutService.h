#pragma once

#include <QObject>
#include <QDBusObjectPath>
#include <QKeySequence>
#include <QDBusArgument>
#include <QVariantMap>

namespace productivity::infrastructure {

struct ShortcutRequest {
    QString id;
    QVariantMap options;
};

QDBusArgument& operator<<(QDBusArgument& argument, const ShortcutRequest& request);
const QDBusArgument& operator>>(const QDBusArgument& argument, ShortcutRequest& request);

class GlobalShortcutService final : public QObject
{
    Q_OBJECT
public:
    explicit GlobalShortcutService(QKeySequence shortcut, QObject* parent = nullptr);
    bool configureQuickNoteShortcut();
    bool setQuickNoteShortcut(const QKeySequence& shortcut);
    bool supportsInAppConfiguration() const { return usingKde_; }
    bool supportsPortalConfiguration() const { return usingPortal_; }
    QString status() const { return status_; }

signals:
    void quickNoteShortcutActivated();
    void statusChanged(const QString& status);

private slots:
    void handleSessionCreated(uint response, const QVariantMap& results);
    void handleShortcutsBound(uint response, const QVariantMap& results);
    void handleShortcutActivated(const QDBusObjectPath& session, const QString& shortcutId,
                                 uint timestamp, const QVariantMap& options);
    void handleConfigurationFinished(uint response, const QVariantMap& results);
    void handleKdeShortcutPressed(const QString& componentUnique, const QString& shortcutUnique,
                                  qlonglong timestamp);

private:
    void setStatus(const QString& status);
    bool bindShortcut();
    bool registerKdeShortcut(const QKeySequence& shortcut);

    QDBusObjectPath sessionHandle_;
    QString status_;
    QKeySequence shortcut_;
    bool usingPortal_ = false;
    bool usingKde_ = false;
    QString kdeComponentPath_;
};

} // namespace productivity::infrastructure

Q_DECLARE_METATYPE(productivity::infrastructure::ShortcutRequest)
