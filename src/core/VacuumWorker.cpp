#include "VacuumWorker.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

VacuumWorker::VacuumWorker(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_databasePath(databasePath)
{
}

void VacuumWorker::run()
{
    const QString connectionName = QStringLiteral("egoboard-vacuum");
    {
        auto db = QSqlDatabase::contains(connectionName)
            ? QSqlDatabase::database(connectionName)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(m_databasePath);
        if (!db.open()) {
            qWarning("egoboard: vacuum cannot open database: %s",
                     qPrintable(db.lastError().text()));
            emit finished(false, QFile(m_databasePath).size());
            return;
        }
        QSqlQuery query(db);
        bool ok = query.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        ok = query.exec(QStringLiteral("VACUUM")) && ok;
        if (!ok)
            qWarning("egoboard: vacuum failed: %s", qPrintable(query.lastError().text()));
        db.close();
        emit finished(ok, QFile(m_databasePath).size());
    }
    QSqlDatabase::removeDatabase(connectionName);
}
