#pragma once

#include <QKeySequence>
#include <QString>

namespace sb::keys {

// One key plus modifiers, independent of any platform.
struct KeyStroke {
    int key = 0;                        // Qt::Key
    Qt::KeyboardModifiers modifiers;    // only Shift, Control, Alt and Meta
    bool keypad = false;

    bool isValid() const { return key != 0 && key != Qt::Key_unknown; }
};

// Uses the first combination of the sequence. A sequence holding a lone
// modifier (e.g. just Ctrl, for push-to-talk) yields that modifier as the key.
KeyStroke fromSequence(const QKeySequence &sequence);
KeyStroke fromPortableText(const QString &text);
QKeySequence toSequence(const KeyStroke &stroke);

// Undoes Shift on US-layout symbols ('!' -> '1', '?' -> '/') so that
// Shift+1 is stored as Shift+1 rather than Shift+!.
int unshiftedKey(int key);
bool isModifierKey(int key);

// XKB keysym name such as "a", "F1" or "KP_1"; empty if unknown.
QString xkbKeysymName(const KeyStroke &stroke);
// Trigger string in the XDG shortcuts format, e.g. "CTRL+SHIFT+a".
QString portalTrigger(const KeyStroke &stroke);
// Win32 virtual-key code, 0 if unknown.
unsigned windowsVirtualKey(const KeyStroke &stroke);
// Linux evdev KEY_* code, 0 if unknown.
unsigned linuxKeyCode(const KeyStroke &stroke);

} // namespace sb::keys
