#pragma once

#include <QObject>

// Runs SQLite VACUUM on its own database connection from a worker QThread,
// so the GUI-thread connection is not the one issuing the (locking) command.
class VacuumWorker : public QObject {
    Q_OBJECT
public:
    explicit VacuumWorker(const QString &databasePath, QObject *parent = nullptr);

public slots:
    void run();

signals:
    void finished(bool ok, qint64 databaseSizeBytes);

private:
    QString m_databasePath;
};
