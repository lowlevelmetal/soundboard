#include "PortalHotkeyBackend.h"

#include "KeyMapping.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>

#include <utility>

namespace sb {

struct PortalShortcut {
    QString id;
    QVariantMap properties;
};

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
    return argument;
}

} // namespace sb

Q_DECLARE_METATYPE(sb::PortalShortcut)

namespace sb {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kShortcutsInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString kRequestInterface = QStringLiteral("org.freedesktop.portal.Request");
const QString kSessionInterface = QStringLiteral("org.freedesktop.portal.Session");
const QString kRegistryInterface = QStringLiteral("org.freedesktop.host.portal.Registry");
const QString kConnectionName = QStringLiteral("soundboard-global-shortcuts");
const char *const kResponseSlot = SLOT(onResponse(uint,QVariantMap,QDBusMessage));

constexpr int kMaxRegisterAttempts = 10;

// GNOME describes triggers as "Press <Shift><Control>F9"; turn that into the
// "Shift+Ctrl+F9" style used everywhere else.
QString readableTrigger(QString description)
{
    if (description.startsWith(QLatin1String("Press ")))
        description.remove(0, 6);
    static const std::pair<const char *, const char *> modifiers[] = {
        {"<Control>", "Ctrl+"}, {"<Primary>", "Ctrl+"}, {"<Ctrl>", "Ctrl+"}, {"<Shift>", "Shift+"},
        {"<Alt>", "Alt+"}, {"<Super>", "Meta+"}, {"<Meta>", "Meta+"}};
    for (const auto &[from, to] : modifiers)
        description.replace(QLatin1String(from), QLatin1String(to), Qt::CaseInsensitive);
    return description;
}

// Enable with QT_LOGGING_RULES="soundboard.hotkeys.debug=true".
Q_LOGGING_CATEGORY(lcPortal, "soundboard.hotkeys", QtWarningMsg)

} // namespace

bool PortalHotkeyBackend::isSupported()
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    message << kShortcutsInterface << QStringLiteral("version");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

PortalHotkeyBackend::PortalHotkeyBackend(const QString &appId, QObject *parent)
    : HotkeyBackend(parent)
    // A private connection: the portal ties the app id to the connection, and
    // Qt may already have used the shared session bus connection.
    , m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, kConnectionName))
    , m_appId(appId)
    , m_debounce(new QTimer(this))
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(400);
    connect(m_debounce, &QTimer::timeout, this, &PortalHotkeyBackend::rebind);

    const bool subscribed =
        m_bus.connect(kService, kPath, kShortcutsInterface, QStringLiteral("Activated"), this,
                      SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    qCDebug(lcPortal) << "connection" << m_bus.baseService() << "Activated subscribed:" << subscribed;
    registerApp();
}

PortalHotkeyBackend::~PortalHotkeyBackend()
{
    closeSession();
    m_bus.disconnect(kService, kPath, kShortcutsInterface, QStringLiteral("Activated"), this,
                     SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    QDBusConnection::disconnectFromBus(kConnectionName);
}

void PortalHotkeyBackend::registerApp()
{
    // Host (non-sandboxed) apps must tell the portal who they are. The portal
    // looks the id up as a desktop entry, which may have only just been
    // written, so a "not found" is retried for a few seconds.
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kRegistryInterface,
                                                          QStringLiteral("Register"));
    message << m_appId << QVariantMap();
    const QDBusMessage reply = m_bus.call(message, QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage
        && reply.errorMessage().contains(QLatin1String("not found"), Qt::CaseInsensitive)
        && ++m_registerAttempts < kMaxRegisterAttempts) {
        QTimer::singleShot(500, this, &PortalHotkeyBackend::registerApp);
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage)
        qCWarning(lcPortal) << "App registration failed:" << reply.errorMessage();
    else
        qCDebug(lcPortal) << "Registered as" << m_appId;
    m_registered = true;
    rebind();
}

void PortalHotkeyBackend::setBindings(const QList<HotkeyBinding> &bindings)
{
    m_bindings = bindings;
    m_debounce->start();
}

void PortalHotkeyBackend::rebind()
{
    if (!m_registered)
        return; // registerApp() calls back once it's done
    if (m_busy) {
        m_dirty = true;
        return;
    }
    // Only ids and triggers matter to the desktop; a renamed sound keeps its shortcut.
    auto sameShortcuts = [](const QList<HotkeyBinding> &a, const QList<HotkeyBinding> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i) {
            if (a[i].id != b[i].id || a[i].sequence != b[i].sequence)
                return false;
        }
        return true;
    };
    if (!m_session.isEmpty() && sameShortcuts(m_bindings, m_boundBindings))
        return;

    closeSession();
    m_boundBindings.clear();
    if (m_bindings.isEmpty()) {
        m_busy = true;
        finish({}, {});
        return;
    }
    m_busy = true;
    createSession();
}

void PortalHotkeyBackend::createSession()
{
    sendRequest(QStringLiteral("CreateSession"), {}, RequestKind::CreateSession);
}

void PortalHotkeyBackend::bindShortcuts()
{
    m_boundBindings = m_bindings;
    m_portalIds.clear();
    QList<PortalShortcut> shortcuts;
    for (const HotkeyBinding &binding : std::as_const(m_boundBindings)) {
        const QString id = portalId(binding);
        m_portalIds.insert(id, binding.id);
        QVariantMap properties{{QStringLiteral("description"), binding.description}};
        const QString trigger = keys::portalTrigger(keys::fromSequence(binding.sequence));
        if (!trigger.isEmpty())
            properties.insert(QStringLiteral("preferred_trigger"), trigger);
        shortcuts.append({id, properties});
    }
    sendRequest(QStringLiteral("BindShortcuts"),
                {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(shortcuts),
                 QString()},
                RequestKind::BindShortcuts);
}

void PortalHotkeyBackend::closeSession()
{
    if (m_session.isEmpty())
        return;
    m_bus.asyncCall(QDBusMessage::createMethodCall(kService, m_session, kSessionInterface,
                                                   QStringLiteral("Close")));
    m_session.clear();
}

void PortalHotkeyBackend::sendRequest(const QString &method, QVariantList arguments,
                                      RequestKind kind)
{
    const QString token = QStringLiteral("soundboard%1").arg(++m_tokenCounter);
    QVariantMap options{{QStringLiteral("handle_token"), token}};
    if (kind == RequestKind::CreateSession)
        options.insert(QStringLiteral("session_handle_token"),
                       QStringLiteral("soundboard_session%1").arg(m_tokenCounter));
    arguments.append(options);

    // Subscribe to the Request's Response before calling, so it can't be missed.
    QString sender = m_bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString path =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    m_bus.connect(kService, path, kRequestInterface, QStringLiteral("Response"), this,
                  kResponseSlot);
    m_pending.insert(path, kind);

    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kShortcutsInterface, method);
    call.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, path, kind](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusPendingReply<QDBusObjectPath> reply = *w;
                if (reply.isError()) {
                    if (m_pending.remove(path))
                        m_bus.disconnect(kService, path, kRequestInterface,
                                         QStringLiteral("Response"), this, kResponseSlot);
                    failAll(tr("The desktop portal rejected the request: %1")
                                .arg(reply.error().message()));
                    return;
                }
                const QString actual = reply.value().path();
                if (actual != path && m_pending.remove(path)) {
                    // Old portals ignore handle_token and pick their own path.
                    m_bus.disconnect(kService, path, kRequestInterface,
                                     QStringLiteral("Response"), this, kResponseSlot);
                    m_bus.connect(kService, actual, kRequestInterface,
                                  QStringLiteral("Response"), this, kResponseSlot);
                    m_pending.insert(actual, kind);
                }
            });
}

void PortalHotkeyBackend::onResponse(uint response, const QVariantMap &results,
                                     const QDBusMessage &message)
{
    const QString path = message.path();
    qCDebug(lcPortal) << "Response" << response << "for" << path << results.keys();
    const auto it = m_pending.constFind(path);
    if (it == m_pending.constEnd())
        return;
    const RequestKind kind = it.value();
    m_pending.erase(it);
    m_bus.disconnect(kService, path, kRequestInterface, QStringLiteral("Response"), this,
                     kResponseSlot);

    if (kind == RequestKind::CreateSession) {
        if (response != 0) {
            failAll(tr("The desktop refused to create a global shortcuts session."));
            return;
        }
        const QVariant handle = results.value(QStringLiteral("session_handle"));
        m_session = handle.metaType() == QMetaType::fromType<QDBusObjectPath>()
                        ? handle.value<QDBusObjectPath>().path()
                        : handle.toString();
        qCDebug(lcPortal) << "Session" << m_session;
        bindShortcuts();
        return;
    }

    if (response == 1) {
        failAll(tr("The shortcuts were not approved in the desktop's dialog."));
        return;
    }
    if (response != 0) {
        failAll(tr("The desktop could not register the shortcuts."));
        return;
    }

    QList<PortalShortcut> shortcuts;
    const QVariant value = results.value(QStringLiteral("shortcuts"));
    if (value.canConvert<QDBusArgument>())
        value.value<QDBusArgument>() >> shortcuts;

    QHash<QString, QString> errors;
    QHash<QString, QString> effective;
    QSet<QString> seen;
    for (const PortalShortcut &shortcut : std::as_const(shortcuts)) {
        const QString bindingId = m_portalIds.value(shortcut.id);
        if (bindingId.isEmpty())
            continue;
        seen.insert(bindingId);
        const QString trigger =
            shortcut.properties.value(QStringLiteral("trigger_description")).toString();
        qCDebug(lcPortal) << "Bound" << shortcut.id << "to" << trigger;
        if (trigger.isEmpty()) {
            errors.insert(bindingId, tr("No key is assigned to this shortcut in the desktop's "
                                        "keyboard settings."));
            continue;
        }
        // Only report the trigger when the user picked a different one in the
        // desktop's dialog than the one Soundboard asked for.
        const QString readable = readableTrigger(trigger);
        const QKeySequence assigned(readable, QKeySequence::PortableText);
        for (const HotkeyBinding &binding : std::as_const(m_boundBindings)) {
            if (binding.id == bindingId && !assigned.isEmpty()
                && keys::toSequence(keys::fromSequence(assigned)) != binding.sequence)
                effective.insert(bindingId, readable);
        }
    }
    for (const HotkeyBinding &binding : std::as_const(m_boundBindings)) {
        if (!seen.contains(binding.id))
            errors.insert(binding.id, tr("The desktop did not accept this shortcut."));
    }
    finish(errors, effective);
}

void PortalHotkeyBackend::onActivated(const QDBusObjectPath &session, const QString &shortcutId,
                                      qulonglong timestamp, const QVariantMap &options)
{
    Q_UNUSED(timestamp);
    Q_UNUSED(options);
    qCDebug(lcPortal) << "Activated" << shortcutId << "session" << session.path();
    if (session.path() != m_session)
        return;
    const QString id = m_portalIds.value(shortcutId);
    if (!id.isEmpty())
        emit activated(id);
}

void PortalHotkeyBackend::finish(const QHash<QString, QString> &errors,
                                 const QHash<QString, QString> &effective)
{
    m_busy = false;
    emit bindingsApplied(errors, effective);
    if (m_dirty) {
        m_dirty = false;
        rebind();
    }
}

void PortalHotkeyBackend::failAll(const QString &reason)
{
    qCWarning(lcPortal) << reason;
    closeSession();
    m_boundBindings.clear();
    QHash<QString, QString> errors;
    for (const HotkeyBinding &binding : std::as_const(m_bindings))
        errors.insert(binding.id, reason);
    emit statusChanged(reason);
    finish(errors, {});
}

QString PortalHotkeyBackend::portalId(const HotkeyBinding &binding) const
{
    // Including the trigger in the id makes a changed hotkey a new shortcut,
    // so the desktop offers it for approval instead of keeping the old key.
    QString id = binding.id + QLatin1Char('_')
                 + keys::portalTrigger(keys::fromSequence(binding.sequence));
    for (QChar &c : id) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-'))
            c = QLatin1Char('.');
    }
    return id;
}

} // namespace sb
