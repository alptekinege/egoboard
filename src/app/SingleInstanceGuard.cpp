#include "SingleInstanceGuard.h"

#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>

SingleInstanceGuard::SingleInstanceGuard(const QString &lockFilePath, const QString &serverName,
                                         QObject *parent)
    : QObject(parent)
    , m_lockFilePath(lockFilePath)
    , m_serverName(serverName)
{
}

SingleInstanceGuard::~SingleInstanceGuard() = default;

bool SingleInstanceGuard::tryLock()
{
    QDir().mkpath(QFileInfo(m_lockFilePath).absolutePath());
    m_lockFile = std::make_unique<QLockFile>(m_lockFilePath);
    m_lockFile->setStaleLockTime(0);
    if (!m_lockFile->tryLock(50)) {
        m_lockFile.reset();
        return false;
    }

    QLocalServer::removeServer(m_serverName);
    m_server = new QLocalServer(this);
    if (!m_server->listen(m_serverName)) {
        qWarning("egoboard: cannot listen on local server %s: %s", qPrintable(m_serverName),
                 qPrintable(m_server->errorString()));
        return true; // single instance still guaranteed by the lock file
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        QLocalSocket *client = m_server->nextPendingConnection();
        if (!client)
            return;
        connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
        connect(client, &QLocalSocket::readyRead, this, [this, client] {
            if (client->readAll().startsWith("show"))
                emit showRequested();
        });
    });
    return true;
}

bool SingleInstanceGuard::sendShow()
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(300))
        return false;
    socket.write("show\n");
    socket.flush();
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}
