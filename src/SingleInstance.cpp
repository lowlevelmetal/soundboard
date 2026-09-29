#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

namespace sb {

namespace {

QString userKey()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
    return QString::fromLatin1(
        QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

} // namespace

SingleInstance::SingleInstance(const QString &name, QObject *parent)
    : QObject(parent)
    , m_serverName(name + QLatin1Char('-') + userKey())
    , m_lock(std::make_unique<QLockFile>(QDir::temp().filePath(m_serverName + QLatin1String(".lock"))))
{
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::claim()
{
    // QLockFile notices locks left behind by a crashed process.
    if (!m_lock->tryLock(100)) {
        QLocalSocket socket;
        socket.connectToServer(m_serverName);
        if (socket.waitForConnected(1000)) {
            socket.write("show\n");
            socket.waitForBytesWritten(1000);
        }
        return false;
    }

    QLocalServer::removeServer(m_serverName); // stale socket from a crash
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_serverName)) {
        qWarning("Single instance server failed: %s", qPrintable(m_server->errorString()));
        return true;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (socket->readAll().startsWith("show"))
                    emit activationRequested();
                socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    return true;
}

} // namespace sb
