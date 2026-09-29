#include "X11HotkeyBackend.h"

#include "KeyMapping.h"

#include <QCoreApplication>
#include <QGuiApplication>

// X11 headers define macros such as None and KeyPress; include them last.
#include <X11/Xlib.h>
#include <xcb/xcb.h>

namespace sb {

namespace {

Display *x11Display()
{
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
        return x11->display();
    return nullptr;
}

bool g_grabFailed = false;

int grabErrorHandler(Display *, XErrorEvent *event)
{
    if (event->error_code == BadAccess)
        g_grabFailed = true;
    return 0;
}

// Grabs must also match with Caps Lock and Num Lock on.
constexpr unsigned kLockMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
constexpr unsigned kModifierMask = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

unsigned x11Modifiers(Qt::KeyboardModifiers modifiers)
{
    unsigned mask = 0;
    if (modifiers.testFlag(Qt::ShiftModifier))
        mask |= ShiftMask;
    if (modifiers.testFlag(Qt::ControlModifier))
        mask |= ControlMask;
    if (modifiers.testFlag(Qt::AltModifier))
        mask |= Mod1Mask;
    if (modifiers.testFlag(Qt::MetaModifier))
        mask |= Mod4Mask;
    return mask;
}

} // namespace

bool X11HotkeyBackend::isSupported()
{
    return x11Display() != nullptr;
}

X11HotkeyBackend::X11HotkeyBackend(QObject *parent)
    : HotkeyBackend(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

X11HotkeyBackend::~X11HotkeyBackend()
{
    ungrabAll();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

void X11HotkeyBackend::ungrabAll()
{
    Display *display = x11Display();
    if (!display) {
        m_grabs.clear();
        return;
    }
    const Window root = DefaultRootWindow(display);
    for (const Grab &grab : std::as_const(m_grabs)) {
        for (unsigned lock : kLockMasks)
            XUngrabKey(display, static_cast<int>(grab.keycode), grab.modifiers | lock, root);
    }
    XSync(display, False);
    m_grabs.clear();
}

void X11HotkeyBackend::setBindings(const QList<HotkeyBinding> &bindings)
{
    ungrabAll();
    Display *display = x11Display();
    QHash<QString, QString> errors;
    if (!display) {
        for (const HotkeyBinding &binding : bindings)
            errors.insert(binding.id, tr("No X11 display connection."));
        emit bindingsApplied(errors, {});
        return;
    }

    const Window root = DefaultRootWindow(display);
    const XErrorHandler previousHandler = XSetErrorHandler(grabErrorHandler);
    for (const HotkeyBinding &binding : bindings) {
        const keys::KeyStroke stroke = keys::fromSequence(binding.sequence);
        const QByteArray name = keys::xkbKeysymName(stroke).toLatin1();
        const KeySym keysym = name.isEmpty() ? NoSymbol : XStringToKeysym(name.constData());
        const KeyCode keycode = keysym == NoSymbol ? 0 : XKeysymToKeycode(display, keysym);
        if (keycode == 0) {
            errors.insert(binding.id, tr("This key is not on the current keyboard layout."));
            continue;
        }
        const unsigned modifiers = x11Modifiers(stroke.modifiers);
        g_grabFailed = false;
        for (unsigned lock : kLockMasks)
            XGrabKey(display, keycode, modifiers | lock, root, True, GrabModeAsync, GrabModeAsync);
        XSync(display, False);
        if (g_grabFailed) {
            for (unsigned lock : kLockMasks)
                XUngrabKey(display, keycode, modifiers | lock, root);
            errors.insert(binding.id, tr("Another application already uses this hotkey."));
            continue;
        }
        m_grabs.append({keycode, modifiers, binding.id});
    }
    XSync(display, False);
    XSetErrorHandler(previousHandler);
    emit bindingsApplied(errors, {});
}

bool X11HotkeyBackend::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "xcb_generic_event_t" || m_grabs.isEmpty())
        return false;
    const auto *event = static_cast<const xcb_generic_event_t *>(message);
    const uint8_t type = event->response_type & ~0x80;
    if (type != XCB_KEY_PRESS && type != XCB_KEY_RELEASE)
        return false;

    const auto *key = reinterpret_cast<const xcb_key_press_event_t *>(event);
    const unsigned modifiers = key->state & kModifierMask;
    for (const Grab &grab : std::as_const(m_grabs)) {
        if (grab.keycode != key->detail || grab.modifiers != modifiers)
            continue;
        if (type == XCB_KEY_RELEASE) {
            m_lastRelease.insert(grab.keycode, key->time);
            return true;
        }
        // Auto-repeat arrives as a release and press with the same timestamp.
        if (m_lastRelease.value(grab.keycode) != key->time)
            emit activated(grab.id);
        return true;
    }
    return false;
}

} // namespace sb
