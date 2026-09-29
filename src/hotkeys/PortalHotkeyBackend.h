#pragma once

#include "HotkeyBackend.h"

#include <QDBusConnection>
#include <QVariantMap>

class QDBusMessage;
class QDBusObjectPath;
class QTimer;

namespace sb {

// Global hotkeys on Wayland through the XDG desktop portal
// (org.freedesktop.portal.GlobalShortcuts). The desktop owns the shortcuts:
// binding them may show a confirmation dialog, and the user can change the
// trigger there, so the effective trigger is reported back.
class PortalHotkeyBackend : public HotkeyBackend {
    Q_OBJECT

public:
    static bool isSupported();

    PortalHotkeyBackend(const QString &appId, QObject *parent = nullptr);
    ~PortalHotkeyBackend() override;

    QString name() const override { return tr("desktop portal"); }
    bool canSuspend() const override { return false; }
    void setBindings(const QList<HotkeyBinding> &bindings) override;

private slots:
    void onResponse(uint response, const QVariantMap &results, const QDBusMessage &message);
    void onActivated(const QDBusObjectPath &session, const QString &shortcutId,
                     qulonglong timestamp, const QVariantMap &options);

private:
    enum class RequestKind { CreateSession, BindShortcuts };

    void registerApp();
    void rebind();
    void createSession();
    void bindShortcuts();
    void closeSession();
    void finish(const QHash<QString, QString> &errors, const QHash<QString, QString> &effective);
    void failAll(const QString &reason);
    void sendRequest(const QString &method, QVariantList arguments, RequestKind kind);
    QString portalId(const HotkeyBinding &binding) const;

    QDBusConnection m_bus;
    QString m_appId;
    int m_registerAttempts = 0;
    bool m_registered = false;
    QString m_session;
    QList<HotkeyBinding> m_bindings;
    QList<HotkeyBinding> m_boundBindings;
    QHash<QString, QString> m_portalIds; // portal shortcut id -> binding id
    QHash<QString, RequestKind> m_pending;
    QTimer *m_debounce = nullptr;
    int m_tokenCounter = 0;
    bool m_busy = false;
    bool m_dirty = false;
};

} // namespace sb
