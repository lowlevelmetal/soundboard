#pragma once

#include "HotkeyBackend.h"

#include <QAbstractNativeEventFilter>

namespace sb {

// Global hotkeys on Windows via RegisterHotKey.
class WindowsHotkeyBackend : public HotkeyBackend, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit WindowsHotkeyBackend(QObject *parent = nullptr);
    ~WindowsHotkeyBackend() override;

    QString name() const override { return QStringLiteral("Windows"); }
    void setBindings(const QList<HotkeyBinding> &bindings) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void unregisterAll();

    QHash<int, QString> m_registered; // hotkey id -> binding id
    int m_nextId = 1;
};

} // namespace sb
