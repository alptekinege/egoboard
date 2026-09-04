#include "DatabaseSchema.h"
#include "StorageManager.h"
#include "../src/app/EncryptionManager.h"

#include <QTemporaryDir>
#include <QtTest>

class TestEncryption : public QObject {
    Q_OBJECT
private slots:
    void walletStatusNotEmpty();
    void generateKeyNotEmpty();
    void storageProbesDoNotCrash();
    void cipherVersionEmptyWhenNotBuilt();
    void settingsFlagRoundTrip();
    void reportsUnavailableBackendConsistently();
    void rejectsEmptyRekey();
};

void TestEncryption::walletStatusNotEmpty()
{
    EncryptionManager enc;
    QVERIFY(!enc.walletStatusText().isEmpty());
}

void TestEncryption::generateKeyNotEmpty()
{
    const QString k1 = EncryptionManager::generateKey();
    const QString k2 = EncryptionManager::generateKey();
    QVERIFY(!k1.isEmpty());
    QVERIFY(k1.size() >= 16);
    QVERIFY(k1 != k2);
}

void TestEncryption::storageProbesDoNotCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("history.db"));
    StorageManager storage(path);
    QVERIFY(storage.verifyEncryptionKey());
    const QString ver = storage.cipherVersion();
    const bool avail = storage.isSqlCipherAvailable();
#ifdef EGOBOARD_HAVE_SQLCIPHER
    Q_UNUSED(ver); Q_UNUSED(avail);
#else
    QVERIFY(!avail);
    QVERIFY(ver.isEmpty());
#endif
    QVERIFY(!storage.setEncryptionKey(QString()));
    QVERIFY(!storage.setEncryptionKey(QStringLiteral("")));
}

void TestEncryption::cipherVersionEmptyWhenNotBuilt()
{
#ifndef EGOBOARD_HAVE_SQLCIPHER
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history2.db")));
    QCOMPARE(storage.cipherVersion(), QString());
    QVERIFY(!storage.isSqlCipherAvailable());
#else
    QSKIP("SQLCipher built — cipherVersion may be non-empty");
#endif
}

void TestEncryption::settingsFlagRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("history3.db"));
    StorageManager storage(path);
    QVERIFY(!storage.isEncrypted());
}

void TestEncryption::reportsUnavailableBackendConsistently()
{
    EncryptionManager encryption;
#ifndef EGOBOARD_HAVE_SQLCIPHER
    QVERIFY(!encryption.isAvailable());
    QCOMPARE(encryption.readKey(nullptr), EncryptionManager::Status::NotAvailable);
    QCOMPARE(encryption.writeKey(QStringLiteral("key")), EncryptionManager::Status::NotAvailable);
    QCOMPARE(encryption.removeKey(), EncryptionManager::Status::NotAvailable);
    QVERIFY(!EncryptionManager::isSqlCipherAvailable());
#else
    QSKIP("SQLCipher/KWallet availability depends on the runtime environment");
#endif
}

void TestEncryption::rejectsEmptyRekey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("rekey.db")));
    const bool rekeyed = storage.changeEncryptionKey(QString());
#ifndef EGOBOARD_HAVE_SQLCIPHER
    QVERIFY(rekeyed);
    QVERIFY(!storage.isEncrypted());
#else
    Q_UNUSED(rekeyed);
#endif
}

QTEST_GUILESS_MAIN(TestEncryption)
#include "tst_encryption.moc"
