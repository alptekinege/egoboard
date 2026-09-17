#include "EncryptionManager.h"

#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlQuery>

#ifdef EGOBOARD_HAVE_SQLCIPHER
#include <KWallet>
#endif

namespace {
constexpr const char kWalletFolder[] = "egoboard";
constexpr const char kWalletEntry[] = "dbKey";
}

EncryptionManager::EncryptionManager(QObject *parent)
    : QObject(parent)
{
}

bool EncryptionManager::isAvailable() const
{
#ifdef EGOBOARD_HAVE_SQLCIPHER
    if (!KWallet::Wallet::isEnabled())
        return false;
    return true;
#else
    return false;
#endif
}

QString EncryptionManager::walletStatusText() const
{
#ifdef EGOBOARD_HAVE_SQLCIPHER
    if (!KWallet::Wallet::isEnabled())
        return QStringLiteral("KWallet disabled");
    return QStringLiteral("KWallet enabled");
#else
    return QStringLiteral("SQLCipher not built (cmake -DEGOBOARD_USE_SQLCIPHER=ON)");
#endif
}

EncryptionManager::Status EncryptionManager::readKey(QString *outKey) const
{
#ifdef EGOBOARD_HAVE_SQLCIPHER
    if (!KWallet::Wallet::isEnabled())
        return Status::WalletDisabled;
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet)
        return Status::WalletOpenFailed;
    if (!wallet->hasFolder(QStringLiteral(kWalletFolder))) {
        delete wallet;
        return Status::EntryMissing;
    }
    wallet->setFolder(QStringLiteral(kWalletFolder));
    if (!wallet->hasEntry(QStringLiteral(kWalletEntry))) {
        delete wallet;
        return Status::EntryMissing;
    }
    QByteArray data;
    if (wallet->readEntry(QStringLiteral(kWalletEntry), data) != 0) {
        delete wallet;
        return Status::EntryMissing;
    }
    delete wallet;
    if (outKey)
        *outKey = QString::fromUtf8(data);
    return data.isEmpty() ? Status::EntryMissing : Status::Ok;
#else
    Q_UNUSED(outKey)
    return Status::NotAvailable;
#endif
}

EncryptionManager::Status EncryptionManager::writeKey(const QString &key) const
{
#ifdef EGOBOARD_HAVE_SQLCIPHER
    if (!KWallet::Wallet::isEnabled())
        return Status::WalletDisabled;
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet)
        return Status::WalletOpenFailed;
    if (!wallet->hasFolder(QStringLiteral(kWalletFolder)))
        wallet->createFolder(QStringLiteral(kWalletFolder));
    wallet->setFolder(QStringLiteral(kWalletFolder));
    const int rc = wallet->writeEntry(QStringLiteral(kWalletEntry), key.toUtf8());
    delete wallet;
    if (rc != 0)
        return Status::WalletOpenFailed;
    return Status::Ok;
#else
    Q_UNUSED(key)
    return Status::NotAvailable;
#endif
}

EncryptionManager::Status EncryptionManager::removeKey() const
{
#ifdef EGOBOARD_HAVE_SQLCIPHER
    if (!KWallet::Wallet::isEnabled())
        return Status::WalletDisabled;
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet)
        return Status::WalletOpenFailed;
    if (wallet->hasFolder(QStringLiteral(kWalletFolder))) {
        wallet->setFolder(QStringLiteral(kWalletFolder));
        if (wallet->hasEntry(QStringLiteral(kWalletEntry))) {
            const int rc = wallet->removeEntry(QStringLiteral(kWalletEntry));
            delete wallet;
            return rc == 0 ? Status::Ok : Status::WalletOperationFailed;
        }
    }
    delete wallet;
    return Status::Ok; // nothing stored, nothing to remove
#else
    return Status::NotAvailable;
#endif
}

QString EncryptionManager::generateKey()
{
    // QRandomGenerator::system() draws from the OS CSPRNG (getrandom /
    // /dev/urandom); global() is a fast, non-cryptographic PRNG and must not
    // be used for key material.
    QByteArray bytes(32, 0);
    QRandomGenerator::system()->generate(reinterpret_cast<quint32 *>(bytes.data()),
                                         reinterpret_cast<quint32 *>(bytes.data() + bytes.size()));
    return QString::fromLatin1(bytes.toBase64());
}

bool EncryptionManager::isSqlCipherAvailable()
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid()) {
        const QString connectionName = QStringLiteral("egoboard-probe-cipher");
        bool ok = false;
        {
            QSqlDatabase tmp = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            tmp.setDatabaseName(QStringLiteral(":memory:"));
            tmp.open();
            QSqlQuery q(tmp);
            ok = q.exec(QStringLiteral("PRAGMA cipher_version")) && q.next()
                && !q.value(0).toString().trimmed().isEmpty();
            tmp.close();
            tmp = QSqlDatabase(); // release the handle before removing the connection
        }
        QSqlDatabase::removeDatabase(connectionName);
        return ok;
    }
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("PRAGMA cipher_version")) && q.next())
        return !q.value(0).toString().trimmed().isEmpty();
    return false;
}
