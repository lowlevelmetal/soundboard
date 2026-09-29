#pragma once

#include "hotkeys/KeyMapping.h"

#include <QString>

namespace sb {

// Holds a key down while sounds play, for voice chat apps that use
// push-to-talk. Uses SendInput on Windows and a uinput virtual keyboard on
// Linux (works on both X11 and Wayland).
class KeyPresser {
public:
    KeyPresser() = default;
    ~KeyPresser();
    KeyPresser(const KeyPresser &) = delete;
    KeyPresser &operator=(const KeyPresser &) = delete;

    // `portableKey` is a key combination or a lone modifier ("Ctrl").
    bool setKey(const QString &portableKey, QString *error);
    void clear();

    void press();
    void release();
    bool isPressed() const { return m_pressed; }

private:
    void send(bool down);

    keys::KeyStroke m_stroke;
    bool m_pressed = false;
#ifdef Q_OS_LINUX
    bool openDevice(QString *error);
    void emitKey(unsigned code, bool down);
    int m_fd = -1;
#endif
};

} // namespace sb
