#include "DatabaseSchema.h"

#include <QSqlError>
#include <QSqlQuery>

namespace DatabaseSchema {

static bool columnExists(QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery q(db);
    // Use PRAGMA table_info which is stable; pragma_table_info table-valued function
    // does not reliably support bound parameters for the table name in all SQLite builds.
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

static bool ensureFts(QSqlDatabase &db)
{
    // Handle migration from older 2-column FTS to 3-column (adds ocr_text).
    // Also handles ocr_text column addition to entries table.
    if (!columnExists(db, QStringLiteral("entries"), QStringLiteral("ocr_text"))) {
        QSqlQuery alter(db);
        // Ignore error if column already exists (race).
        alter.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN ocr_text TEXT"));
    }

    // Detect old FTS schema without ocr_text -> drop and recreate
    bool needsRecreate = false;
    {
        QSqlQuery sq(db);
        if (sq.exec(QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name='entries_fts'")) && sq.next()) {
            const QString sql = sq.value(0).toString();
            if (!sql.contains(QStringLiteral("ocr_text"))) needsRecreate = true;
        }
    }
    if (needsRecreate) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("DROP TRIGGER IF EXISTS entries_ai"));
        q.exec(QStringLiteral("DROP TRIGGER IF EXISTS entries_ad"));
        q.exec(QStringLiteral("DROP TRIGGER IF EXISTS entries_au"));
        q.exec(QStringLiteral("DROP TABLE IF EXISTS entries_fts"));
    }

    QSqlQuery probe(db);
    if (!probe.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts "
            "USING fts5(preview, text_data, ocr_text, content='entries', content_rowid='id', "
            "tokenize='unicode61')"))) {
        qWarning("egoboard: FTS5 not available, falling back to LIKE search: %s",
                 qPrintable(probe.lastError().text()));
        return false;
    }

    static const QList<QString> triggers = {
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS entries_ai AFTER INSERT ON entries BEGIN "
            "INSERT INTO entries_fts(rowid, preview, text_data, ocr_text) "
            "VALUES (new.id, new.preview, new.text_data, new.ocr_text); END"),
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS entries_ad AFTER DELETE ON entries BEGIN "
            "INSERT INTO entries_fts(entries_fts, rowid, preview, text_data, ocr_text) "
            "VALUES ('delete', old.id, old.preview, old.text_data, old.ocr_text); END"),
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS entries_au AFTER UPDATE ON entries BEGIN "
            "INSERT INTO entries_fts(entries_fts, rowid, preview, text_data, ocr_text) "
            "VALUES ('delete', old.id, old.preview, old.text_data, old.ocr_text); "
            "INSERT INTO entries_fts(rowid, preview, text_data, ocr_text) "
            "VALUES (new.id, new.preview, new.text_data, new.ocr_text); END"),
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
            " ocr_text TEXT,"
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
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS snippets ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name TEXT NOT NULL,"
            " template TEXT NOT NULL,"
            " shortcut TEXT,"
            " created_ms INTEGER NOT NULL)"),
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
