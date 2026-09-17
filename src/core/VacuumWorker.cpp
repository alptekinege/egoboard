#include "VacuumWorker.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <atomic>

VacuumWorker::VacuumWorker(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_databasePath(databasePath)
{
}

void VacuumWorker::run()
{
    // One connection per run: queued vacuums must not share a connection name,
    // and Qt requires the handle to be released before removeDatabase().
    static std::atomic_int connectionCounter{0};
    const QString connectionName =
        QStringLiteral("egoboard-vacuum-%1").arg(connectionCounter.fetch_add(1));

    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(m_databasePath);
        if (!db.open()) {
            qWarning("egoboard: vacuum cannot open database: %s",
                     qPrintable(db.lastError().text()));
        } else {
            QSqlQuery query(db);
            ok = query.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
            ok = query.exec(QStringLiteral("VACUUM")) && ok;
            if (!ok)
                qWarning("egoboard: vacuum failed: %s", qPrintable(query.lastError().text()));
            db.close();
        }
        db = QSqlDatabase(); // release the handle before removing the connection
    }
    QSqlDatabase::removeDatabase(connectionName);
    emit finished(ok, QFile(m_databasePath).size());
}
