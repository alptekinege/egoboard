#include <QtTest>

#include "SingleInstanceGuard.h"

#include <QLocalSocket>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#include <sys/wait.h>
#include <unistd.h>
#endif

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void primaryAcquiresLock();
    void secondaryFailsLockAndTriggersShow();
    void lockFreedOnDestruction();
    void sendShowFailsWithoutServer();
    void ignoresNonShowMessages();
    void recoversLockLeftBehindByCrash();

private:
    QTemporaryDir m_tempDir;
    QString m_lockPath;
    QString m_serverName;
};

void TestSingleInstance::init()
{
    const quint64 id = QRandomGenerator::global()->generate64();
    m_lockPath = m_tempDir.filePath(QStringLiteral("instance-%1.lock").arg(id));
    m_serverName = QStringLiteral("egoboard-test-%1").arg(id);
}

void TestSingleInstance::primaryAcquiresLock()
{
    SingleInstanceGuard guard(m_lockPath, m_serverName);
    QVERIFY(guard.tryLock());
}

void TestSingleInstance::secondaryFailsLockAndTriggersShow()
{
    auto primary = std::make_unique<SingleInstanceGuard>(m_lockPath, m_serverName);
    QVERIFY(primary->tryLock());

    QSignalSpy showSpy(primary.get(), &SingleInstanceGuard::showRequested);

    // Second guard with identical lock path and server name
    SingleInstanceGuard secondary(m_lockPath, m_serverName);
    QVERIFY(!secondary.tryLock());

    // Second instance sends show request to the first
    QVERIFY(secondary.sendShow());

    // First instance should receive showRequested
    QTRY_COMPARE(showSpy.count(), 1);
}

void TestSingleInstance::lockFreedOnDestruction()
{
    {
        SingleInstanceGuard guard1(m_lockPath, m_serverName);
        QVERIFY(guard1.tryLock());
    } // guard1 destroyed here, lock and socket server cleaned up

    // guard2 can now acquire the lock
    SingleInstanceGuard guard2(m_lockPath, m_serverName);
    QVERIFY(guard2.tryLock());
}

void TestSingleInstance::sendShowFailsWithoutServer()
{
    SingleInstanceGuard guard(m_lockPath, m_serverName);
    QVERIFY(!guard.sendShow());
}

void TestSingleInstance::ignoresNonShowMessages()
{
    SingleInstanceGuard primary(m_lockPath, m_serverName);
    QVERIFY(primary.tryLock());
    QSignalSpy showSpy(&primary, &SingleInstanceGuard::showRequested);

    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    QVERIFY(socket.waitForConnected(300));
    QVERIFY(socket.write("hide\n") > 0);
    QVERIFY(socket.waitForBytesWritten(300));
    socket.disconnectFromServer();
    QTest::qWait(50);
    QCOMPARE(showSpy.count(), 0);
}

void TestSingleInstance::recoversLockLeftBehindByCrash()
{
#ifdef Q_OS_UNIX
    // Child takes the lock and exits without unlocking (simulated crash), so
    // the lock file survives with a dead PID.
    const pid_t pid = fork();
    if (pid == 0) {
        SingleInstanceGuard child(m_lockPath, m_serverName);
        _exit(child.tryLock() ? 0 : 1);
    }
    QVERIFY(pid > 0);
    int status = 0;
    QCOMPARE(waitpid(pid, &status, 0), pid);
    QVERIFY(WIFEXITED(status));
    QCOMPARE(WEXITSTATUS(status), 0);

    // Startup must recover instead of blocking forever on the stale lock.
    SingleInstanceGuard guard(m_lockPath, m_serverName);
    QVERIFY(guard.tryLock());
#else
    QSKIP("POSIX-only crash simulation");
#endif
}

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
