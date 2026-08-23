#include "DatabaseSchema.h"

#include <QSqlError>
#include <QSqlQuery>

namespace DatabaseSchema {

static bool ensureFts(QSqlDatabase &db)
{
    // FTS5 virtual table for fast full-text search over preview + text_data.
    // Uses external content sync ('entries' table) so the index stays small and
    // rebuildable. Gracefully no-ops if the SQLite build lacks FTS5.
    QSqlQuery probe(db);
    if (!probe.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts "
            "USING fts5(preview, text_data, content='entries', content_rowid='id', "
            "tokenize='unicode61')"))) {
        qWarning("egoboard: FTS5 not available, falling back to LIKE search: %s",
                 qPrintable(probe.lastError().text()));
        return false;
    }

    static const QList<QString> triggers = {
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS entries_ai AFTER INSERT ON entries BEGIN "
            "INSERT INTO entries_fts(rowid, preview, text_data) "
            "VALUES (new.id, new.preview, new.text_data); END"),
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS entries_ad AFTER DELETE ON entries BEGIN "
            "INSERT INTO entries_fts(entries_fts, rowid, preview, text_data) "
            "VALUES ('delete', old.id, old.preview, old.text_data); END"),
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS entries_au AFTER UPDATE ON entries BEGIN "
            "INSERT INTO entries_fts(entries_fts, rowid, preview, text_data) "
            "VALUES ('delete', old.id, old.preview, old.text_data); "
            "INSERT INTO entries_fts(rowid, preview, text_data) "
            "VALUES (new.id, new.preview, new.text_data); END"),
    };
    for (const QString &sql : triggers) {
        QSqlQuery q(db);
        if (!q.exec(sql))
            qWarning("egoboard: FTS trigger failed: %s (%s)",
                     qPrintable(q.lastError().text()), qPrintable(sql));
    }

    // Backfill / rebuild: idempotent, handles existing DBs without FTS rows.
    {
        QSqlQuery rebuild(db);
        QSqlQuery countFts(db);
        QSqlQuery countEntries(db);
        bool needsRebuild = true;
        if (countFts.exec(QStringLiteral("SELECT COUNT(*) FROM entries_fts"))
            && countFts.next() && countEntries.exec(QStringLiteral("SELECT COUNT(*) FROM entries"))
            && countEntries.next()) {
            needsRebuild = countFts.value(0).toLongLong() != countEntries.value(0).toLongLong();
        }
        if (needsRebuild) {
            if (!rebuild.exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES('rebuild')")))
                qWarning("egoboard: FTS rebuild failed: %s", qPrintable(rebuild.lastError().text()));
        }
    }
    return true;
}

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
    ensureFts(db);
    return true;
}

} // namespace DatabaseSchema
