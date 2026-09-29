#include "HotkeyManager.h"

#include "KeyMapping.h"

#include <QGuiApplication>

#if defined(Q_OS_WIN)
#include "WindowsHotkeyBackend.h"
#elif defined(Q_OS_LINUX)
#include "PortalHotkeyBackend.h"
#ifdef SOUNDBOARD_HAVE_X11
#include "X11HotkeyBackend.h"
#endif
#endif

namespace sb {

HotkeyManager::HotkeyManager(const QString &appId, QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(appId);
    m_backend = new WindowsHotkeyBackend(this);
#elif defined(Q_OS_LINUX)
    const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
                         || qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
    if (wayland && PortalHotkeyBackend::isSupported())
        m_backend = new PortalHotkeyBackend(appId, this);
#ifdef SOUNDBOARD_HAVE_X11
    if (!m_backend && X11HotkeyBackend::isSupported()) {
        m_backend = new X11HotkeyBackend(this);
        if (wayland)
            m_status = tr("This Wayland desktop has no global shortcuts portal. Hotkeys use X11 "
                          "and only work while an X11 window has focus.");
    }
#endif
    if (!m_backend)
        m_status = wayland ? tr("Global hotkeys are unavailable: this Wayland desktop does not "
                                "provide the GlobalShortcuts portal.")
                           : tr("Global hotkeys are unavailable on this platform.");
#else
    Q_UNUSED(appId);
    m_status = tr("Global hotkeys are unavailable on this platform.");
#endif

    if (m_backend) {
        connect(m_backend, &HotkeyBackend::activated, this, [this](const QString &id) {
            if (!m_suspended)
                emit activated(id);
        });
        connect(m_backend, &HotkeyBackend::bindingsApplied, this,
                &HotkeyManager::onBindingsApplied);
        connect(m_backend, &HotkeyBackend::statusChanged, this, [this](const QString &status) {
            m_status = status;
            emit statusChanged(status);
        });
    }
}

QString HotkeyManager::backendName() const
{
    return m_backend ? m_backend->name() : tr("None");
}

void HotkeyManager::setBindings(const QList<HotkeyBinding> &bindings)
{
    m_localErrors.clear();
    QList<HotkeyBinding> accepted;
    QHash<QString, QString> owners; // normalised sequence -> description
    for (const HotkeyBinding &binding : bindings) {
        if (binding.sequence.isEmpty())
            continue;
        const keys::KeyStroke stroke = keys::fromSequence(binding.sequence);
#if defined(Q_OS_WIN)
        const bool supported = keys::windowsVirtualKey(stroke) != 0;
#else
        const bool supported = !keys::xkbKeysymName(stroke).isEmpty();
#endif
        if (!stroke.isValid() || keys::isModifierKey(stroke.key) || !supported) {
            m_localErrors.insert(binding.id, tr("This key can't be used as a global hotkey."));
            continue;
        }
        const QKeySequence normalised = keys::toSequence(stroke);
        const QString key = normalised.toString(QKeySequence::PortableText);
        if (owners.contains(key)) {
            m_localErrors.insert(binding.id,
                                 tr("The same hotkey is already used by \"%1\".").arg(owners[key]));
            continue;
        }
        owners.insert(key, binding.description);
        accepted.append({binding.id, normalised, binding.description});
    }

    m_accepted = accepted;
    if (!m_backend) {
        for (const HotkeyBinding &binding : std::as_const(accepted))
            m_localErrors.insert(binding.id, m_status);
        onBindingsApplied({}, {});
        return;
    }
    if (!m_suspended || !m_backend->canSuspend())
        m_backend->setBindings(accepted);
}

void HotkeyManager::setSuspended(bool suspended)
{
    if (suspended == m_suspended)
        return;
    m_suspended = suspended;
    if (m_backend && m_backend->canSuspend())
        m_backend->setBindings(suspended ? QList<HotkeyBinding>() : m_accepted);
}

void HotkeyManager::onBindingsApplied(const QHash<QString, QString> &errors,
                                      const QHash<QString, QString> &effective)
{
    if (m_suspended && m_backend && m_backend->canSuspend())
        return; // the empty set applied while suspended says nothing
    m_errors = m_localErrors;
    m_errors.insert(errors);
    m_effective = effective;
    emit errorsChanged();
}

} // namespace sb
