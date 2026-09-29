#include "KeyCaptureEdit.h"

#include "hotkeys/KeyMapping.h"

#include <QKeyEvent>

namespace sb {

namespace {

QString modifierName(int key)
{
    switch (key) {
    case Qt::Key_Shift:
        return QStringLiteral("Shift");
    case Qt::Key_Control:
        return QStringLiteral("Ctrl");
    case Qt::Key_Alt:
        return QStringLiteral("Alt");
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        return QStringLiteral("Meta");
    default:
        return {};
    }
}

QString readableText(const QString &portable)
{
    if (portable.isEmpty())
        return {};
    if (keys::isModifierKey(keys::fromPortableText(portable).key))
        return portable; // a lone modifier, e.g. "Ctrl"
    return QKeySequence(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

} // namespace

KeyCaptureEdit::KeyCaptureEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setClearButtonEnabled(true);
    setPlaceholderText(tr("Click and press a key…"));
    setContextMenuPolicy(Qt::NoContextMenu); // no pasting arbitrary text
    setAcceptDrops(false);
    connect(this, &QLineEdit::textChanged, this, [this](const QString &text) {
        // The clear button empties the text directly.
        if (text.isEmpty() && !m_portable.isEmpty())
            commit({});
    });
}

void KeyCaptureEdit::setPortableText(const QString &text)
{
    m_portable = text;
    updateText();
}

void KeyCaptureEdit::commit(const QString &portable)
{
    const bool changed = portable != m_portable;
    m_portable = portable;
    updateText();
    if (changed)
        emit keyChanged(m_portable);
}

void KeyCaptureEdit::updateText()
{
    const QSignalBlocker blocker(this);
    setText(readableText(m_portable));
}

bool KeyCaptureEdit::event(QEvent *event)
{
    // Take every key while focused, including ones bound to app shortcuts.
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    return QLineEdit::event(event);
}

void KeyCaptureEdit::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_unknown || key == 0) {
        event->accept();
        return;
    }
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        QLineEdit::keyPressEvent(event); // keep keyboard navigation working
        return;
    }
    event->accept();
    if (keys::isModifierKey(key)) {
        m_pendingModifier = key;
        return;
    }
    m_pendingModifier = 0;

    Qt::KeyboardModifiers modifiers = event->modifiers()
                                      & (Qt::ShiftModifier | Qt::ControlModifier
                                         | Qt::AltModifier | Qt::MetaModifier);
    if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) && modifiers == Qt::NoModifier) {
        commit({});
        return;
    }
    keys::KeyStroke stroke;
    stroke.keypad = event->modifiers().testFlag(Qt::KeypadModifier);
    stroke.modifiers = modifiers;
    stroke.key = modifiers.testFlag(Qt::ShiftModifier) && !stroke.keypad ? keys::unshiftedKey(key)
                                                                          : key;
    commit(keys::toSequence(stroke).toString(QKeySequence::PortableText));
}

void KeyCaptureEdit::keyReleaseEvent(QKeyEvent *event)
{
    event->accept();
    if (m_allowModifierOnly && m_pendingModifier != 0 && event->key() == m_pendingModifier) {
        const QString name = modifierName(m_pendingModifier);
        if (!name.isEmpty())
            commit(name);
    }
    m_pendingModifier = 0;
}

void KeyCaptureEdit::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    setPlaceholderText(tr("Press a key combination…"));
}

void KeyCaptureEdit::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    setPlaceholderText(tr("Click and press a key…"));
    m_pendingModifier = 0;
}

} // namespace sb
