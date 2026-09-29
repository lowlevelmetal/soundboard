#include "KeyPresser.h"

#include <QCoreApplication>

#include <algorithm>
#include <vector>

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#ifndef MAPVK_VK_TO_VSC_EX
#define MAPVK_VK_TO_VSC_EX 4
#endif
#endif

namespace sb {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("KeyPresser", text);
}

// The keys to press, modifiers first.
std::vector<keys::KeyStroke> strokesFor(const keys::KeyStroke &stroke)
{
    std::vector<keys::KeyStroke> strokes;
    const std::pair<Qt::KeyboardModifier, Qt::Key> modifiers[] = {
        {Qt::ControlModifier, Qt::Key_Control},
        {Qt::ShiftModifier, Qt::Key_Shift},
        {Qt::AltModifier, Qt::Key_Alt},
        {Qt::MetaModifier, Qt::Key_Meta},
    };
    for (const auto &[flag, key] : modifiers) {
        if (stroke.modifiers.testFlag(flag))
            strokes.push_back({key, {}, false});
    }
    strokes.push_back({stroke.key, {}, stroke.keypad});
    return strokes;
}

} // namespace

KeyPresser::~KeyPresser()
{
    clear();
}

bool KeyPresser::setKey(const QString &portableKey, QString *error)
{
    release();
    m_stroke = {};
    const keys::KeyStroke stroke = keys::fromPortableText(portableKey);
    if (!stroke.isValid()) {
        if (error)
            *error = tr("No push-to-talk key is set.");
        return false;
    }
#if defined(Q_OS_LINUX)
    if (keys::linuxKeyCode(stroke) == 0) {
        if (error)
            *error = tr("This key can't be simulated.");
        return false;
    }
    if (!openDevice(error))
        return false;
#elif defined(Q_OS_WIN)
    if (keys::windowsVirtualKey(stroke) == 0) {
        if (error)
            *error = tr("This key can't be simulated.");
        return false;
    }
#else
    if (error)
        *error = tr("Push-to-talk is not supported on this platform.");
    return false;
#endif
    m_stroke = stroke;
    return true;
}

void KeyPresser::clear()
{
    release();
    m_stroke = {};
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        ioctl(m_fd, UI_DEV_DESTROY);
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

void KeyPresser::press()
{
    if (m_pressed || !m_stroke.isValid())
        return;
    send(true);
    m_pressed = true;
}

void KeyPresser::release()
{
    if (!m_pressed)
        return;
    send(false);
    m_pressed = false;
}

#if defined(Q_OS_LINUX)

bool KeyPresser::openDevice(QString *error)
{
    if (m_fd >= 0)
        return true;
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (error)
            *error = tr("Can't open /dev/uinput (%1). Push-to-talk needs write access to it; "
                        "see the README for a udev rule.")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int code = 1; code < 256; ++code)
        ioctl(fd, UI_SET_KEYBIT, code);

    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1;
    setup.id.product = 0x1;
    std::strncpy(setup.name, "Soundboard push-to-talk", UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        if (error)
            *error = tr("Can't create the virtual keyboard: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(fd);
        return false;
    }
    m_fd = fd;
    return true;
}

void KeyPresser::emitKey(unsigned code, bool down)
{
    input_event events[2]{};
    events[0].type = EV_KEY;
    events[0].code = static_cast<__u16>(code);
    events[0].value = down ? 1 : 0;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;
    if (::write(m_fd, events, sizeof(events)) < 0)
        qWarning("uinput write failed: %s", std::strerror(errno));
}

void KeyPresser::send(bool down)
{
    if (m_fd < 0)
        return;
    auto strokes = strokesFor(m_stroke);
    if (!down)
        std::reverse(strokes.begin(), strokes.end());
    for (const keys::KeyStroke &stroke : strokes)
        emitKey(keys::linuxKeyCode(stroke), down);
}

#elif defined(Q_OS_WIN)

void KeyPresser::send(bool down)
{
    auto strokes = strokesFor(m_stroke);
    if (!down)
        std::reverse(strokes.begin(), strokes.end());
    std::vector<INPUT> inputs;
    for (const keys::KeyStroke &stroke : strokes) {
        const UINT vk = keys::windowsVirtualKey(stroke);
        const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(vk);
        input.ki.wScan = static_cast<WORD>(scan & 0xFF);
        // Scan codes reach games that read raw input as well as normal apps.
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        if (scan != 0)
            input.ki.dwFlags |= KEYEVENTF_SCANCODE;
        if ((scan & 0xFF00) == 0xE000)
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        inputs.push_back(input);
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

#else

void KeyPresser::send(bool)
{
}

#endif

} // namespace sb
