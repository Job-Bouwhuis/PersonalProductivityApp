#include "GlobalShortcutService.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QKeySequence>

namespace productivity::infrastructure {
namespace {

const QString portalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString portalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString shortcutsInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");

} // namespace

QDBusArgument& operator<<(QDBusArgument& argument, const ShortcutRequest& request)
{
    argument.beginStructure();
    argument << request.id << request.options;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, ShortcutRequest& request)
{
    argument.beginStructure();
    argument >> request.id >> request.options;
    argument.endStructure();
    return argument;
}

namespace {

QString requestPath(const QDBusMessage& reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    const QVariant value = reply.arguments().first();
    if (value.canConvert<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value.toString();
}

QVariantMap unwrap(const QVariantMap& wrapped)
{
    QVariantMap result;
    for (auto it = wrapped.cbegin(); it != wrapped.cend(); ++it) {
        const auto variant = it.value().value<QDBusVariant>();
        result.insert(it.key(), variant.variant().isValid() ? variant.variant() : it.value());
    }
    return result;
}

} // namespace

GlobalShortcutService::GlobalShortcutService(QKeySequence shortcut, QObject* parent)
    : QObject(parent)
    , shortcut_(std::move(shortcut))
{
    qDBusRegisterMetaType<ShortcutRequest>();
    qDBusRegisterMetaType<QList<ShortcutRequest>>();

    QDBusInterface portal(portalService, portalPath, shortcutsInterface,
                          QDBusConnection::sessionBus());
    if (!portal.isValid()) {
        if (!registerKdeShortcut(shortcut_))
            setStatus(QStringLiteral("This desktop has no supported global shortcut service."));
        return;
    }
    usingPortal_ = true;
    const QVariantMap options{{QStringLiteral("session_handle_token"), QStringLiteral("quick_notes")}};
    const QString path = requestPath(portal.call(QStringLiteral("CreateSession"), options));
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Could not create a global shortcut session."));
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    if (!bus.connect(portalService, path, requestInterface, QStringLiteral("Response"),
                     this, SLOT(handleSessionCreated(uint,QVariantMap)))) {
        setStatus(QStringLiteral("Could not listen for the global shortcut session."));
        return;
    }
    setStatus(QStringLiteral("Setting up the quick-note shortcut…"));
}

bool GlobalShortcutService::bindShortcut()
{
    QString preferredTrigger = shortcut_.toString(QKeySequence::PortableText);
    preferredTrigger.replace(QStringLiteral("Ctrl"), QStringLiteral("<Control>"));
    preferredTrigger.replace(QStringLiteral("Alt"), QStringLiteral("<Alt>"));
    preferredTrigger.replace(QStringLiteral("Shift"), QStringLiteral("<Shift>"));
    preferredTrigger.replace(QStringLiteral("Meta"), QStringLiteral("<Super>"));
    preferredTrigger.remove(QLatin1Char('+'));
    QList<ShortcutRequest> shortcuts;
    shortcuts.append({QStringLiteral("quick-note"), {
        {QStringLiteral("description"), QStringLiteral("Open quick-note capture")},
        {QStringLiteral("preferred_trigger"), preferredTrigger}
    }});
    const QVariantMap options{{QStringLiteral("handle_token"), QStringLiteral("quick_note_bind")}};
    QDBusInterface portal(portalService, portalPath, shortcutsInterface,
                          QDBusConnection::sessionBus());
    const QString path = requestPath(portal.call(QStringLiteral("BindShortcuts"),
        QVariant::fromValue(sessionHandle_), QVariant::fromValue(shortcuts), QString(), options));
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Could not register the quick-note shortcut."));
        return false;
    }
    if (!QDBusConnection::sessionBus().connect(portalService, path, requestInterface,
            QStringLiteral("Response"), this, SLOT(handleShortcutsBound(uint,QVariantMap)))) {
        setStatus(QStringLiteral("Could not listen for the quick-note shortcut registration."));
        return false;
    }
    QDBusConnection::sessionBus().connect(portalService, portalPath, shortcutsInterface,
        QStringLiteral("Activated"), this,
        SLOT(handleShortcutActivated(QDBusObjectPath,QString,uint,QVariantMap)));
    return true;
}

void GlobalShortcutService::handleSessionCreated(uint response, const QVariantMap& wrappedResults)
{
    if (response != 0) {
        setStatus(QStringLiteral("The desktop denied global shortcut access."));
        return;
    }
    const QVariantMap results = unwrap(wrappedResults);
    QString path = results.value(QStringLiteral("session_handle")).toString();
    if (path.isEmpty()) {
        const QVariant value = wrappedResults.value(QStringLiteral("session_handle"));
        if (value.canConvert<QDBusObjectPath>())
            path = value.value<QDBusObjectPath>().path();
    }
    if (path.isEmpty()) {
        setStatus(QStringLiteral("The desktop returned no global shortcut session."));
        return;
    }
    sessionHandle_ = QDBusObjectPath(path);
    if (!bindShortcut())
        return;
}

void GlobalShortcutService::handleShortcutsBound(uint response, const QVariantMap&)
{
    setStatus(response == 0 ? QStringLiteral("Quick-note shortcut is ready (%1 by default).")
                                .arg(shortcut_.toString(QKeySequence::NativeText))
                            : QStringLiteral("The quick-note shortcut was not registered."));
}

bool GlobalShortcutService::registerKdeShortcut(const QKeySequence& shortcut)
{
    if (shortcut.isEmpty() || shortcut.count() != 1)
        return false;
    const QString component = QStringLiteral("personal-productivity-app");
    const QString action = QStringLiteral("quick-note");
    const QStringList actionId{component, action, QStringLiteral("Personal Productivity Service"),
                               QStringLiteral("Open Quick Note")};
    QDBusConnection bus = QDBusConnection::sessionBus();
    qDBusRegisterMetaType<QList<int>>();
    QDBusInterface accelerator(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"),
                               QStringLiteral("org.kde.KGlobalAccel"), bus);
    if (!accelerator.isValid())
        return false;
    const QDBusMessage registerReply = accelerator.call(QStringLiteral("doRegister"), actionId);
    if (registerReply.type() == QDBusMessage::ErrorMessage)
        return false;
    const QDBusMessage componentReply = accelerator.call(QStringLiteral("getComponent"), component);
    if (componentReply.type() != QDBusMessage::ReplyMessage || componentReply.arguments().isEmpty())
        return false;
    const QVariant componentValue = componentReply.arguments().first();
    kdeComponentPath_ = componentValue.canConvert<QDBusObjectPath>()
        ? componentValue.value<QDBusObjectPath>().path() : componentValue.toString();
    if (kdeComponentPath_.isEmpty())
        return false;
    if (!bus.connect(QStringLiteral("org.kde.kglobalaccel"), kdeComponentPath_,
                     QStringLiteral("org.kde.kglobalaccel.Component"),
                     QStringLiteral("globalShortcutPressed"), this,
                     SLOT(handleKdeShortcutPressed(QString,QString,qlonglong))))
        return false;

    constexpr uint flags = 6; // KGlobalAccel: SetPresent (2) | NoAutoloading (4).
    const QList<int> keys{shortcut[0].toCombined()};
    const QDBusMessage setReply = accelerator.call(QStringLiteral("setShortcut"),
        QVariant::fromValue(actionId), QVariant::fromValue(keys), QVariant::fromValue(flags));
    if (setReply.type() != QDBusMessage::ReplyMessage || setReply.arguments().isEmpty())
        return false;
    const QVariant activeKeyValue = setReply.arguments().first();
    QList<int> activeKeys = activeKeyValue.value<QList<int>>();
    if (activeKeys.isEmpty() && activeKeyValue.canConvert<QDBusArgument>())
        activeKeys = qdbus_cast<QList<int>>(activeKeyValue.value<QDBusArgument>());
    if (activeKeys.isEmpty())
        return false;
    usingKde_ = true;
    shortcut_ = shortcut;
    setStatus(QStringLiteral("Quick-note shortcut is ready (%1).")
                  .arg(shortcut_.toString(QKeySequence::NativeText)));
    return true;
}

bool GlobalShortcutService::setQuickNoteShortcut(const QKeySequence& shortcut)
{
    if (usingKde_ || !usingPortal_)
        return registerKdeShortcut(shortcut);
    setStatus(QStringLiteral("Use the desktop shortcut dialog to change this shortcut."));
    return false;
}

void GlobalShortcutService::handleKdeShortcutPressed(const QString& componentUnique,
                                                      const QString& shortcutUnique, qlonglong)
{
    if (componentUnique == QLatin1String("personal-productivity-app") &&
        shortcutUnique == QLatin1String("quick-note"))
        emit quickNoteShortcutActivated();
}

void GlobalShortcutService::handleShortcutActivated(const QDBusObjectPath& session,
                                                     const QString& shortcutId,
                                                     uint, const QVariantMap&)
{
    if (session.path() == sessionHandle_.path() && shortcutId == QLatin1String("quick-note"))
        emit quickNoteShortcutActivated();
}

bool GlobalShortcutService::configureQuickNoteShortcut()
{
    if (sessionHandle_.path().isEmpty()) {
        setStatus(QStringLiteral("The desktop global-shortcut service is not ready."));
        return false;
    }
    QDBusInterface portal(portalService, portalPath, shortcutsInterface,
                          QDBusConnection::sessionBus());
    const QString path = requestPath(portal.call(QStringLiteral("ConfigureShortcuts"),
        QVariant::fromValue(sessionHandle_), QString(), QVariantMap{}));
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Could not open the desktop shortcut settings."));
        return false;
    }
    if (!QDBusConnection::sessionBus().connect(portalService, path, requestInterface,
            QStringLiteral("Response"), this, SLOT(handleConfigurationFinished(uint,QVariantMap)))) {
        setStatus(QStringLiteral("Could not listen for shortcut configuration changes."));
        return false;
    }
    setStatus(QStringLiteral("Choose a shortcut in the desktop dialog."));
    return true;
}

void GlobalShortcutService::handleConfigurationFinished(uint response, const QVariantMap&)
{
    setStatus(response == 0 ? QStringLiteral("Quick-note shortcut updated.")
                            : QStringLiteral("Shortcut configuration was cancelled."));
}

void GlobalShortcutService::setStatus(const QString& status)
{
    if (status_ == status)
        return;
    status_ = status;
    emit statusChanged(status_);
}

} // namespace productivity::infrastructure
