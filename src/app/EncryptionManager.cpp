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
        wallet->removeEntry(QStringLiteral(kWalletEntry));
    }
    delete wallet;
    return Status::Ok;
#else
    return Status::NotAvailable;
#endif
}

QString EncryptionManager::generateKey()
{
    QByteArray bytes(32, 0);
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = char(QRandomGenerator::global()->bounded(256));
    return QString::fromLatin1(bytes.toBase64());
}

bool EncryptionManager::isSqlCipherAvailable()
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid()) {
        QSqlDatabase tmp = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("egoboard-probe-cipher"));
        tmp.setDatabaseName(QStringLiteral(":memory:"));
        tmp.open();
        QSqlQuery q(tmp);
        const bool ok = q.exec(QStringLiteral("PRAGMA cipher_version")) && q.next() && !q.value(0).toString().trimmed().isEmpty();
        tmp.close();
        QSqlDatabase::removeDatabase(QStringLiteral("egoboard-probe-cipher"));
        return ok;
    }
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("PRAGMA cipher_version")) && q.next())
        return !q.value(0).toString().trimmed().isEmpty();
    return false;
}
