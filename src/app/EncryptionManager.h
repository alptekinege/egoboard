#pragma once

#include <QObject>
#include <QString>

class EncryptionManager : public QObject {
    Q_OBJECT
public:
    enum class Status {
        Ok,
        NotAvailable,
        WalletDisabled,
        WalletOpenFailed,
        EntryMissing,
    };

    explicit EncryptionManager(QObject *parent = nullptr);

    bool isAvailable() const;
    QString walletStatusText() const;

    Status readKey(QString *outKey) const;
    Status writeKey(const QString &key) const;
    Status removeKey() const;

    static QString generateKey();
    static bool isSqlCipherAvailable();

signals:
    void keyChanged();
};
