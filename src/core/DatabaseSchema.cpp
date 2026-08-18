#include "DatabaseSchema.h"

#include <QSqlError>
#include <QSqlQuery>

namespace DatabaseSchema {

bool ensure(QSqlDatabase &db)
{
    static const QList<QString> statements = {
        QStringLiteral("PRAGMA journal_mode=WAL"),
        QStringLiteral("PRAGMA synchronous=NORMAL"),
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS entries ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " timestamp_ms INTEGER NOT NULL,"
            " content_type INTEGER NOT NULL,"
            " content_hash TEXT NOT NULL,"
            " text_data TEXT,"
            " blob_data BLOB,"
            " preview TEXT,"
            " size_bytes INTEGER NOT NULL DEFAULT 0,"
            " pinned INTEGER NOT NULL DEFAULT 0,"
            " sensitive INTEGER NOT NULL DEFAULT 0,"
            " use_count INTEGER NOT NULL DEFAULT 0,"
            " source_app TEXT,"
            " source_window TEXT,"
            " UNIQUE(content_hash))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_order ON entries(timestamp_ms DESC, id DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_app ON entries(source_app)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_type ON entries(content_type)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS groups ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " parent_id INTEGER REFERENCES groups(id) ON DELETE CASCADE,"
            " name TEXT NOT NULL,"
            " color TEXT,"
            " icon TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS entry_groups ("
            " entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,"
            " group_id INTEGER NOT NULL REFERENCES groups(id) ON DELETE CASCADE,"
            " PRIMARY KEY(entry_id, group_id))"),
    };

    for (const QString &statement : statements) {
        QSqlQuery query(db);
        if (!query.exec(statement)) {
            qWarning("egoboard: schema statement failed: %s (%s)",
                     qPrintable(query.lastError().text()), qPrintable(statement));
            return false;
        }
    }
    return true;
}

} // namespace DatabaseSchema
