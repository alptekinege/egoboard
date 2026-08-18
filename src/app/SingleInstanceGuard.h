#pragma once

#include <QLocalServer>
#include <QObject>
#include <QLockFile>

#include <memory>

// Ensures only one egoboard instance runs. A second launch signals the first
// one (over a local socket) to show its window, then exits.
class SingleInstanceGuard : public QObject {
    Q_OBJECT
public:
    explicit SingleInstanceGuard(const QString &lockFilePath, const QString &serverName,
                                 QObject *parent = nullptr);
    ~SingleInstanceGuard() override;

    // Attempts to become the running instance. Returns false if another
    // instance already holds the lock (caller should sendShow() and exit).
    bool tryLock();
    bool sendShow();

signals:
    void showRequested();

private:
    QString m_lockFilePath;
    QString m_serverName;
    std::unique_ptr<QLockFile> m_lockFile;
    QLocalServer *m_server = nullptr;
};
