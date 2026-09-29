#pragma once

#include <QLockFile>
#include <QObject>

#include <memory>

class QLocalServer;

namespace sb {

// Makes sure only one soundboard runs per user (two would fight over hotkeys
// and the virtual microphone). A second instance asks the first to show its
// window and exits.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(const QString &name, QObject *parent = nullptr);
    ~SingleInstance() override;

    // True if this is the only instance; false if another one was notified.
    bool claim();

signals:
    void activationRequested();

private:
    QString m_serverName;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer *m_server = nullptr;
};

} // namespace sb
