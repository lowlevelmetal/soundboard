#pragma once

#include "HotkeyBackend.h"

#include <QAbstractNativeEventFilter>

namespace sb {

// Global hotkeys on X11 sessions via XGrabKey on the root window.
class X11HotkeyBackend : public HotkeyBackend, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    static bool isSupported();

    explicit X11HotkeyBackend(QObject *parent = nullptr);
    ~X11HotkeyBackend() override;

    QString name() const override { return QStringLiteral("X11"); }
    void setBindings(const QList<HotkeyBinding> &bindings) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    struct Grab {
        unsigned keycode;
        unsigned modifiers;
        QString id;
    };

    void ungrabAll();

    QList<Grab> m_grabs;
    QHash<unsigned, unsigned long> m_lastRelease; // keycode -> X server time
};

} // namespace sb
