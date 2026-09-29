#include "KeyMapping.h"

#include <QStringList>

namespace sb::keys {

namespace {

struct KeyInfo {
    int qtKey;
    bool keypad;
    const char *xkb;
    unsigned short evdev; // linux/input-event-codes.h
    unsigned short vk;    // winuser.h VK_*
};

// Letters, digits and function keys are computed; everything else is listed.
constexpr KeyInfo kKeys[] = {
    {Qt::Key_Escape, false, "Escape", 1, 0x1B},
    {Qt::Key_Tab, false, "Tab", 15, 0x09},
    {Qt::Key_Backspace, false, "BackSpace", 14, 0x08},
    {Qt::Key_Return, false, "Return", 28, 0x0D},
    {Qt::Key_Enter, false, "Return", 28, 0x0D},
    {Qt::Key_Insert, false, "Insert", 110, 0x2D},
    {Qt::Key_Delete, false, "Delete", 111, 0x2E},
    {Qt::Key_Pause, false, "Pause", 119, 0x13},
    {Qt::Key_Print, false, "Print", 99, 0x2C},
    {Qt::Key_Home, false, "Home", 102, 0x24},
    {Qt::Key_End, false, "End", 107, 0x23},
    {Qt::Key_Left, false, "Left", 105, 0x25},
    {Qt::Key_Up, false, "Up", 103, 0x26},
    {Qt::Key_Right, false, "Right", 106, 0x27},
    {Qt::Key_Down, false, "Down", 108, 0x28},
    {Qt::Key_PageUp, false, "Prior", 104, 0x21},
    {Qt::Key_PageDown, false, "Next", 109, 0x22},
    {Qt::Key_CapsLock, false, "Caps_Lock", 58, 0x14},
    {Qt::Key_NumLock, false, "Num_Lock", 69, 0x90},
    {Qt::Key_ScrollLock, false, "Scroll_Lock", 70, 0x91},
    {Qt::Key_Menu, false, "Menu", 127, 0x5D},
    {Qt::Key_Space, false, "space", 57, 0x20},
    {Qt::Key_Minus, false, "minus", 12, 0xBD},
    {Qt::Key_Equal, false, "equal", 13, 0xBB},
    {Qt::Key_BracketLeft, false, "bracketleft", 26, 0xDB},
    {Qt::Key_BracketRight, false, "bracketright", 27, 0xDD},
    {Qt::Key_Semicolon, false, "semicolon", 39, 0xBA},
    {Qt::Key_Apostrophe, false, "apostrophe", 40, 0xDE},
    {Qt::Key_QuoteLeft, false, "grave", 41, 0xC0},
    {Qt::Key_Backslash, false, "backslash", 43, 0xDC},
    {Qt::Key_Comma, false, "comma", 51, 0xBC},
    {Qt::Key_Period, false, "period", 52, 0xBE},
    {Qt::Key_Slash, false, "slash", 53, 0xBF},
    {Qt::Key_Less, false, "less", 86, 0xE2}, // the extra key on ISO keyboards

    {Qt::Key_Asterisk, true, "KP_Multiply", 55, 0x6A},
    {Qt::Key_Plus, true, "KP_Add", 78, 0x6B},
    {Qt::Key_Minus, true, "KP_Subtract", 74, 0x6D},
    {Qt::Key_Period, true, "KP_Decimal", 83, 0x6E},
    {Qt::Key_Comma, true, "KP_Decimal", 83, 0x6E},
    {Qt::Key_Slash, true, "KP_Divide", 98, 0x6F},
    {Qt::Key_Enter, true, "KP_Enter", 96, 0x0D},

    // Lone modifiers are only useful as a push-to-talk key.
    {Qt::Key_Shift, false, "Shift_L", 42, 0xA0},
    {Qt::Key_Control, false, "Control_L", 29, 0xA2},
    {Qt::Key_Alt, false, "Alt_L", 56, 0xA4},
    {Qt::Key_AltGr, false, "ISO_Level3_Shift", 100, 0xA5},
    {Qt::Key_Meta, false, "Super_L", 125, 0x5B},
    {Qt::Key_Super_L, false, "Super_L", 125, 0x5B},
    {Qt::Key_Super_R, false, "Super_R", 126, 0x5C},

    {Qt::Key_MediaPlay, false, "XF86AudioPlay", 164, 0xB3},
    {Qt::Key_MediaTogglePlayPause, false, "XF86AudioPlay", 164, 0xB3},
    {Qt::Key_MediaStop, false, "XF86AudioStop", 166, 0xB2},
    {Qt::Key_MediaPrevious, false, "XF86AudioPrev", 165, 0xB1},
    {Qt::Key_MediaNext, false, "XF86AudioNext", 163, 0xB0},
    {Qt::Key_VolumeMute, false, "XF86AudioMute", 113, 0xAD},
    {Qt::Key_VolumeDown, false, "XF86AudioLowerVolume", 114, 0xAE},
    {Qt::Key_VolumeUp, false, "XF86AudioRaiseVolume", 115, 0xAF},
};

// evdev codes of A..Z (QWERTY positions).
constexpr unsigned short kLetterCodes[26] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                             49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
// evdev codes of 0..9 on the number row and on the keypad.
constexpr unsigned short kDigitCodes[10] = {11, 2, 3, 4, 5, 6, 7, 8, 9, 10};
constexpr unsigned short kKeypadDigitCodes[10] = {82, 79, 80, 81, 75, 76, 77, 71, 72, 73};

struct Resolved {
    QString xkb;
    unsigned evdev = 0;
    unsigned vk = 0;
};

Resolved resolve(const KeyStroke &stroke)
{
    const int key = stroke.key;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        const int i = key - Qt::Key_A;
        return {QString(QChar('a' + i)), kLetterCodes[i], static_cast<unsigned>(key)};
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        const int digit = key - Qt::Key_0;
        if (stroke.keypad)
            return {QStringLiteral("KP_%1").arg(digit), kKeypadDigitCodes[digit],
                    static_cast<unsigned>(0x60 + digit)};
        return {QString::number(digit), kDigitCodes[digit], static_cast<unsigned>(key)};
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        const int n = key - Qt::Key_F1 + 1;
        unsigned evdev = 0;
        if (n <= 10)
            evdev = 58 + n;
        else if (n <= 12)
            evdev = 76 + n;
        else
            evdev = 170 + n;
        return {QStringLiteral("F%1").arg(n), evdev, static_cast<unsigned>(0x6F + n)};
    }
    const KeyInfo *fallback = nullptr;
    for (const KeyInfo &info : kKeys) {
        if (info.qtKey != key)
            continue;
        if (info.keypad == stroke.keypad)
            return {QString::fromLatin1(info.xkb), info.evdev, info.vk};
        if (!fallback)
            fallback = &info;
    }
    if (fallback)
        return {QString::fromLatin1(fallback->xkb), fallback->evdev, fallback->vk};
    return {};
}

constexpr Qt::KeyboardModifiers kModifierMask =
    Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

} // namespace

bool isModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
        return true;
    default:
        return false;
    }
}

int unshiftedKey(int key)
{
    switch (key) {
    case Qt::Key_Exclam: return Qt::Key_1;
    case Qt::Key_At: return Qt::Key_2;
    case Qt::Key_NumberSign: return Qt::Key_3;
    case Qt::Key_Dollar: return Qt::Key_4;
    case Qt::Key_Percent: return Qt::Key_5;
    case Qt::Key_AsciiCircum: return Qt::Key_6;
    case Qt::Key_Ampersand: return Qt::Key_7;
    case Qt::Key_Asterisk: return Qt::Key_8;
    case Qt::Key_ParenLeft: return Qt::Key_9;
    case Qt::Key_ParenRight: return Qt::Key_0;
    case Qt::Key_Underscore: return Qt::Key_Minus;
    case Qt::Key_Plus: return Qt::Key_Equal;
    case Qt::Key_BraceLeft: return Qt::Key_BracketLeft;
    case Qt::Key_BraceRight: return Qt::Key_BracketRight;
    case Qt::Key_Colon: return Qt::Key_Semicolon;
    case Qt::Key_QuoteDbl: return Qt::Key_Apostrophe;
    case Qt::Key_AsciiTilde: return Qt::Key_QuoteLeft;
    case Qt::Key_Bar: return Qt::Key_Backslash;
    case Qt::Key_Greater: return Qt::Key_Period;
    case Qt::Key_Question: return Qt::Key_Slash;
    case Qt::Key_Backtab: return Qt::Key_Tab;
    default: return key;
    }
}

KeyStroke fromSequence(const QKeySequence &sequence)
{
    KeyStroke stroke;
    if (sequence.isEmpty())
        return stroke;
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    stroke.keypad = modifiers.testFlag(Qt::KeypadModifier);
    stroke.modifiers = modifiers & kModifierMask;
    stroke.key = combination.key();
    if (stroke.modifiers.testFlag(Qt::ShiftModifier) && !stroke.keypad)
        stroke.key = unshiftedKey(stroke.key);
    return stroke;
}

KeyStroke fromPortableText(const QString &text)
{
    if (text.isEmpty())
        return {};
    // QKeySequence cannot parse a lone modifier, which push-to-talk allows.
    static const struct {
        const char *name;
        int key;
    } loneModifiers[] = {{"Shift", Qt::Key_Shift}, {"Ctrl", Qt::Key_Control},
                         {"Alt", Qt::Key_Alt}, {"Meta", Qt::Key_Meta}};
    for (const auto &m : loneModifiers) {
        if (text == QLatin1String(m.name))
            return {m.key, {}, false};
    }
    return fromSequence(QKeySequence(text, QKeySequence::PortableText));
}

QKeySequence toSequence(const KeyStroke &stroke)
{
    Qt::KeyboardModifiers modifiers = stroke.modifiers;
    if (stroke.keypad)
        modifiers |= Qt::KeypadModifier;
    return QKeySequence(QKeyCombination(modifiers, static_cast<Qt::Key>(stroke.key)));
}

QString xkbKeysymName(const KeyStroke &stroke)
{
    return resolve(stroke).xkb;
}

QString portalTrigger(const KeyStroke &stroke)
{
    const QString keysym = xkbKeysymName(stroke);
    if (keysym.isEmpty())
        return {};
    QStringList parts;
    if (stroke.modifiers.testFlag(Qt::ControlModifier))
        parts << QStringLiteral("CTRL");
    if (stroke.modifiers.testFlag(Qt::AltModifier))
        parts << QStringLiteral("ALT");
    if (stroke.modifiers.testFlag(Qt::ShiftModifier))
        parts << QStringLiteral("SHIFT");
    if (stroke.modifiers.testFlag(Qt::MetaModifier))
        parts << QStringLiteral("LOGO");
    parts << keysym;
    return parts.join(QLatin1Char('+'));
}

unsigned windowsVirtualKey(const KeyStroke &stroke)
{
    return resolve(stroke).vk;
}

unsigned linuxKeyCode(const KeyStroke &stroke)
{
    return resolve(stroke).evdev;
}

} // namespace sb::keys
