#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

namespace sb {

struct HotkeyBinding {
    QString id;               // e.g. "sound:12" or "action:stop"
    QKeySequence sequence;
    QString description;      // shown by desktop shortcut dialogs

    bool operator==(const HotkeyBinding &) const = default;
};

// A platform mechanism for system-wide hotkeys.
class HotkeyBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString name() const = 0;
    // Whether bindings can be dropped and re-added cheaply (not on Wayland,
    // where re-binding may ask the user again).
    virtual bool canSuspend() const { return true; }
    // Replaces every registered hotkey with `bindings`. Results are reported
    // through bindingsApplied(), possibly asynchronously.
    virtual void setBindings(const QList<HotkeyBinding> &bindings) = 0;

signals:
    void activated(const QString &id);
    // Hotkeys that could not be registered, id -> reason. `effective` maps ids
    // to the trigger the desktop actually assigned when it differs from the
    // requested one (Wayland lets the user pick).
    void bindingsApplied(const QHash<QString, QString> &errors,
                         const QHash<QString, QString> &effective);
    void statusChanged(const QString &status);
};

} // namespace sb
