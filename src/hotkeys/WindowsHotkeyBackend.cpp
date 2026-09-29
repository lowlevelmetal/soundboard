#include "WindowsHotkeyBackend.h"

#include "KeyMapping.h"

#include <QCoreApplication>

#include <windows.h>

namespace sb {

WindowsHotkeyBackend::WindowsHotkeyBackend(QObject *parent)
    : HotkeyBackend(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowsHotkeyBackend::~WindowsHotkeyBackend()
{
    unregisterAll();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

void WindowsHotkeyBackend::unregisterAll()
{
    for (auto it = m_registered.constBegin(); it != m_registered.constEnd(); ++it)
        UnregisterHotKey(nullptr, it.key());
    m_registered.clear();
}

void WindowsHotkeyBackend::setBindings(const QList<HotkeyBinding> &bindings)
{
    unregisterAll();
    QHash<QString, QString> errors;
    for (const HotkeyBinding &binding : bindings) {
        const keys::KeyStroke stroke = keys::fromSequence(binding.sequence);
        const UINT vk = keys::windowsVirtualKey(stroke);
        if (vk == 0) {
            errors.insert(binding.id, tr("This key can't be used as a global hotkey."));
            continue;
        }
        UINT modifiers = MOD_NOREPEAT;
        if (stroke.modifiers.testFlag(Qt::ShiftModifier))
            modifiers |= MOD_SHIFT;
        if (stroke.modifiers.testFlag(Qt::ControlModifier))
            modifiers |= MOD_CONTROL;
        if (stroke.modifiers.testFlag(Qt::AltModifier))
            modifiers |= MOD_ALT;
        if (stroke.modifiers.testFlag(Qt::MetaModifier))
            modifiers |= MOD_WIN;

        const int id = m_nextId;
        m_nextId = m_nextId >= 0xBFFF ? 1 : m_nextId + 1;
        // A null window posts WM_HOTKEY to this (the GUI) thread's queue.
        if (!RegisterHotKey(nullptr, id, modifiers, vk)) {
            errors.insert(binding.id, tr("Another application already uses this hotkey."));
            continue;
        }
        m_registered.insert(id, binding.id);
    }
    emit bindingsApplied(errors, {});
}

bool WindowsHotkeyBackend::nativeEventFilter(const QByteArray &eventType, void *message,
                                             qintptr *)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    const auto it = m_registered.constFind(static_cast<int>(msg->wParam));
    if (it == m_registered.constEnd())
        return false;
    emit activated(it.value());
    return true;
}

} // namespace sb
