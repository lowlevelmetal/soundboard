#pragma once

#include <QLineEdit>

namespace sb {

// A field that records one key combination when the user presses it.
// Backspace or Delete on their own clear it. Optionally accepts a lone
// modifier (e.g. just Ctrl), which is useful for push-to-talk keys.
class KeyCaptureEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit KeyCaptureEdit(QWidget *parent = nullptr);

    void setAllowModifierOnly(bool allow) { m_allowModifierOnly = allow; }

    // QKeySequence::PortableText, or a modifier name such as "Ctrl".
    QString portableText() const { return m_portable; }
    void setPortableText(const QString &text);

signals:
    void keyChanged(const QString &portableText);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void commit(const QString &portable);
    void updateText();

    QString m_portable;
    bool m_allowModifierOnly = false;
    int m_pendingModifier = 0;
};

} // namespace sb
