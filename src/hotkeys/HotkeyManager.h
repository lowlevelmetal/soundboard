#pragma once

#include "HotkeyBackend.h"

namespace sb {

// Chooses the right backend for the session and filters out duplicate or
// unsupported hotkeys before they reach it.
class HotkeyManager : public QObject {
    Q_OBJECT

public:
    explicit HotkeyManager(const QString &appId, QObject *parent = nullptr);

    bool isAvailable() const { return m_backend != nullptr; }
    QString backendName() const;
    QString status() const { return m_status; }

    void setBindings(const QList<HotkeyBinding> &bindings);
    // While suspended no hotkey fires, so a key field can record any key.
    void setSuspended(bool suspended);
    QHash<QString, QString> errors() const { return m_errors; }
    QHash<QString, QString> effectiveTriggers() const { return m_effective; }

signals:
    void activated(const QString &id);
    void errorsChanged();
    void statusChanged(const QString &status);

private:
    void onBindingsApplied(const QHash<QString, QString> &errors,
                           const QHash<QString, QString> &effective);

    HotkeyBackend *m_backend = nullptr;
    QString m_status;
    bool m_suspended = false;
    QList<HotkeyBinding> m_accepted;
    QHash<QString, QString> m_localErrors;
    QHash<QString, QString> m_errors;
    QHash<QString, QString> m_effective;
};

} // namespace sb
