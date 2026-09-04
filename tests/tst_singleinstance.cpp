#include <QtTest>

#include "SingleInstanceGuard.h"

#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void primaryAcquiresLock();
    void secondaryFailsLockAndTriggersShow();
    void lockFreedOnDestruction();

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

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
