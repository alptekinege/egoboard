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

    // Detect old FTS schema without ocr_text -> drop and recreate. Also note
    // whether the table is missing entirely: creating it leaves an empty index
    // behind, and an update trigger issued against that index corrupts it
    // (external-content FTS5 has no rows of its own to fall back on).
    bool ftsTableExisted = false;
    bool needsRecreate = false;
    {
        QSqlQuery sq(db);
        if (sq.exec(QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name='entries_fts'")) && sq.next()) {
            ftsTableExisted = true;
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
        // A freshly created (or recreated) index is empty even when COUNT(*)
        // over the external-content table reports the content rows.
        bool needsRebuild = needsRecreate || !ftsTableExisted;
        if (!needsRebuild && countFts.exec(QStringLiteral("SELECT COUNT(*) FROM entries_fts"))
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

bool quickCheck(QSqlDatabase &db, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    // quick_check reports one row per problem; a healthy database says "ok".
    bool first = true;
    bool healthy = true;
    while (query.next()) {
        const QString message = query.value(0).toString();
        if (message.compare(QLatin1String("ok"), Qt::CaseInsensitive) != 0) {
            healthy = false;
            if (error && first)
                *error = message;
        }
        first = false;
    }
    if (error && healthy)
        error->clear();
    return healthy;
}

bool rebuildSearchIndex(QSqlDatabase &db)
{
    // Make sure the index and its triggers exist (a missing index is repaired
    // too), then rebuild it from the entries table.
    if (!ensureFts(db))
        return false;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES('rebuild')"))) {
        qWarning("egoboard: FTS rebuild failed: %s", qPrintable(query.lastError().text()));
        return false;
    }
    return true;
}

bool isSqlCipherAvailable(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA cipher_version")))
        return false;
    if (q.next())
        return !q.value(0).toString().trimmed().isEmpty();
    return q.isValid();
}

QString cipherVersion(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("PRAGMA cipher_version")) && q.next())
        return q.value(0).toString();
    return {};
}

static QString escapeKey(const QString &key)
{
    QString out = key;
    out.replace(QStringLiteral("'"), QStringLiteral("''"));
    return out;
}

bool setKey(QSqlDatabase &db, const QString &key)
{
    if (key.isEmpty())
        return false;
    QSqlQuery q(db);
    const QString sql = QStringLiteral("PRAGMA key = '%1'").arg(escapeKey(key));
    if (!q.exec(sql)) {
        qWarning("egoboard: PRAGMA key failed: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return probeKey(db);
}

bool rekey(QSqlDatabase &db, const QString &newKey)
{
    QSqlQuery q(db);
    QString sql;
    if (newKey.isEmpty())
        sql = QStringLiteral("PRAGMA rekey = ''");
    else
        sql = QStringLiteral("PRAGMA rekey = '%1'").arg(escapeKey(newKey));
    if (!q.exec(sql)) {
        qWarning("egoboard: PRAGMA rekey failed: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

bool probeKey(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT count(*) FROM sqlite_master"))) {
        qWarning("egoboard: probeKey failed: %s", qPrintable(q.lastError().text()));
        return false;
    }
    if (!q.next())
        return false;
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
        // MostUsed sort: use_count leads, timestamp/id break ties.
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_use_count ON entries(use_count DESC, timestamp_ms DESC, id DESC)"),
        // Filter-only views keep the ordered entries as a covering subset.
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_pinned ON entries(timestamp_ms DESC, id DESC) WHERE pinned = 1"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_sensitive ON entries(timestamp_ms DESC, id DESC) WHERE sensitive = 1"),
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
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tags ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name TEXT NOT NULL UNIQUE COLLATE NOCASE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS entry_tags ("
            " entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,"
            " tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,"
            " PRIMARY KEY(entry_id, tag_id))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entry_tags_tag ON entry_tags(tag_id)"),
        // Group lookups ("entries in this group") otherwise scan the PK table.
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entry_groups_group ON entry_groups(group_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS saved_searches ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name TEXT NOT NULL UNIQUE,"
            " filter TEXT NOT NULL)"),
        // Legacy rows may differ only by case; keep the lowest id per name so
        // the case-insensitive unique index below can be created.
        QStringLiteral(
            "DELETE FROM saved_searches WHERE id NOT IN "
            "(SELECT MIN(id) FROM saved_searches GROUP BY name COLLATE NOCASE)"),
        // Queries match names with COLLATE NOCASE; enforce the same uniqueness.
        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_saved_searches_name ON saved_searches(name COLLATE NOCASE)"),
        // U11 soft-delete buffer: trashed rows keep their original ids
        // (AUTOINCREMENT never reuses them) plus tag/group links, so Undo
        // restores exactly instead of re-inserting as new rows. Trash is
        // invisible to history queries, stats, dedup and caps; stale rows are
        // purged by age (see StorageManager::purgeTrash). No foreign keys by
        // design: trash must survive the deletion of referenced groups/tags,
        // and restore re-links with OR IGNORE.
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS trash_entries ("
            " id INTEGER PRIMARY KEY,"
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
            " trashed_ms INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_trash_entries_time ON trash_entries(trashed_ms)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS trash_entry_tags ("
            " entry_id INTEGER NOT NULL,"
            " tag_id INTEGER NOT NULL,"
            " PRIMARY KEY(entry_id, tag_id))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS trash_entry_groups ("
            " entry_id INTEGER NOT NULL,"
            " group_id INTEGER NOT NULL,"
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

    // content_hash is declared TEXT but older builds bound it as BLOB. SQLite
    // compares values by storage class, so mixed rows would silently miss
    // dedup hits; normalize legacy rows in place.
    {
        QSqlQuery select(db);
        if (select.exec(QStringLiteral("SELECT id FROM entries WHERE typeof(content_hash) = 'blob'"))) {
            QList<qint64> legacyIds;
            while (select.next())
                legacyIds.append(select.value(0).toLongLong());
            if (!legacyIds.isEmpty()) {
                db.transaction();
                for (const qint64 id : legacyIds) {
                    QSqlQuery update(db);
                    update.prepare(QStringLiteral(
                        "UPDATE OR IGNORE entries SET content_hash = CAST(content_hash AS TEXT)"
                        " WHERE id = :id"));
                    update.bindValue(QStringLiteral(":id"), id);
                    if (!update.exec())
                        qWarning("egoboard: content_hash normalization failed for %lld: %s",
                                 id, qPrintable(update.lastError().text()));
                }
                db.commit();
            }
        }
    }

    ensureFts(db);
    return true;
}

} // namespace DatabaseSchema
