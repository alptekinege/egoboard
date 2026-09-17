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
    // A lock is stale when its process is gone (checked via PID) or, as a
    // fallback when liveness cannot be determined, when it is older than this.
    // Disabling the age fallback would wedge startup on such systems.
    m_lockFile->setStaleLockTime(60 * 1000);
    if (!m_lockFile->tryLock(50)) {
        m_lockFile.reset();
        return false;
    }

    QLocalServer::removeServer(m_serverName);
    m_server = new QLocalServer(this);
    if (!m_server->listen(m_serverName)) {
        qWarning("egoboard: cannot listen on local server %s: %s", qPrintable(m_serverName),
                 qPrintable(m_server->errorString()));
        // A leftover socket file can outlive its process; retry once before
        // degrading to lock-file-only mode (second launches then do nothing).
        QLocalServer::removeServer(m_serverName);
        if (!m_server->listen(m_serverName)) {
            qWarning("egoboard: second-launch window activation is unavailable: %s",
                     qPrintable(m_server->errorString()));
        }
    }
    if (m_server->isListening()) {
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
    }
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
