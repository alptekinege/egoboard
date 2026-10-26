#include "DatabaseSchema.h"

#include <QByteArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <limits>

namespace DatabaseSchema {

namespace {

struct EntrySnapshot {
    qint64 id = 0;
    qint64 timestampMs = 0;
    int contentType = 0;
    QString textData;
    QByteArray blobData;
    QString preview;
    qint64 sizeBytes = 0;
    bool pinned = false;
    bool sensitive = false;
    qint64 useCount = 0;
    QString sourceApp;
    QString sourceWindow;
    QString ocrText;
};

bool readEntry(QSqlDatabase &db, qint64 id, EntrySnapshot *entry)
{
    if (!entry)
        return false;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, timestamp_ms, content_type, text_data, blob_data, preview, size_bytes,"
        " pinned, sensitive, use_count, source_app, source_window, ocr_text"
        " FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return false;
    entry->id = query.value(0).toLongLong();
    entry->timestampMs = query.value(1).toLongLong();
    entry->contentType = query.value(2).toInt();
    entry->textData = query.value(3).toString();
    entry->blobData = query.value(4).toByteArray();
    entry->preview = query.value(5).toString();
    entry->sizeBytes = query.value(6).toLongLong();
    entry->pinned = query.value(7).toBool();
    entry->sensitive = query.value(8).toBool();
    entry->useCount = query.value(9).toLongLong();
    entry->sourceApp = query.value(10).toString();
    entry->sourceWindow = query.value(11).toString();
    entry->ocrText = query.value(12).toString();
    query.finish();
    return true;
}

bool mergeEntries(QSqlDatabase &db, qint64 keepId, qint64 duplicateId,
                  const QString &normalizedHash)
{
    EntrySnapshot keep;
    EntrySnapshot duplicate;
    if (!readEntry(db, keepId, &keep) || !readEntry(db, duplicateId, &duplicate))
        return false;

    const EntrySnapshot &newer = duplicate.timestampMs > keep.timestampMs ? duplicate : keep;
    const EntrySnapshot &older = duplicate.timestampMs > keep.timestampMs ? keep : duplicate;
    EntrySnapshot merged = newer;
    merged.id = keepId;
    merged.timestampMs = qMax(keep.timestampMs, duplicate.timestampMs);
    merged.textData = newer.textData.isNull() ? older.textData : newer.textData;
    merged.preview = newer.preview.isNull() ? older.preview : newer.preview;
    if (newer.blobData.isEmpty() && !older.blobData.isEmpty())
        merged.blobData = older.blobData;
    merged.sizeBytes = qMax(keep.sizeBytes, duplicate.sizeBytes);
    merged.pinned = keep.pinned || duplicate.pinned;
    merged.sensitive = keep.sensitive || duplicate.sensitive;
    const qint64 keepUseCount = qMax<qint64>(0, keep.useCount);
    const qint64 duplicateUseCount = qMax<qint64>(0, duplicate.useCount);
    const qint64 maxUseCount = std::numeric_limits<qint64>::max();
    merged.useCount = keepUseCount > maxUseCount - duplicateUseCount
                          ? maxUseCount
                          : keepUseCount + duplicateUseCount;
    if (newer.sourceApp.isNull())
        merged.sourceApp = older.sourceApp;
    if (newer.sourceWindow.isNull())
        merged.sourceWindow = older.sourceWindow;
    if (newer.ocrText.isEmpty())
        merged.ocrText = older.ocrText;

    static const QStringList linkTables{QStringLiteral("entry_tags"),
                                        QStringLiteral("entry_groups")};
    for (const QString &table : linkTables) {
        QSqlQuery transfer(db);
        transfer.prepare(QStringLiteral("INSERT OR IGNORE INTO %1 (entry_id, %2) "
                                        "SELECT :keep, %2 FROM %1 WHERE entry_id = :duplicate")
                             .arg(table, table == QLatin1String("entry_tags")
                                             ? QStringLiteral("tag_id")
                                             : QStringLiteral("group_id")));
        transfer.bindValue(QStringLiteral(":keep"), keepId);
        transfer.bindValue(QStringLiteral(":duplicate"), duplicateId);
        if (!transfer.exec()) {
            qWarning("egoboard: cannot merge legacy entry links from %lld: %s",
                     duplicateId, qPrintable(transfer.lastError().text()));
            return false;
        }
    }

    QSqlQuery removeDuplicate(db);
    removeDuplicate.prepare(QStringLiteral("DELETE FROM entries WHERE id = :id"));
    removeDuplicate.bindValue(QStringLiteral(":id"), duplicateId);
    if (!removeDuplicate.exec() || removeDuplicate.numRowsAffected() != 1) {
        qWarning("egoboard: cannot remove merged legacy entry %lld: %s",
                 duplicateId, qPrintable(removeDuplicate.lastError().text()));
        return false;
    }

    QSqlQuery update(db);
    update.prepare(QStringLiteral(
        "UPDATE entries SET timestamp_ms = :ts, content_type = :type, content_hash = :hash,"
        " text_data = :text, blob_data = :blob, preview = :preview, size_bytes = :size,"
        " pinned = :pinned, sensitive = :sensitive, use_count = :uses,"
        " source_app = :app, source_window = :window, ocr_text = :ocr WHERE id = :id"));
    update.bindValue(QStringLiteral(":ts"), merged.timestampMs);
    update.bindValue(QStringLiteral(":type"), merged.contentType);
    update.bindValue(QStringLiteral(":hash"), normalizedHash);
    update.bindValue(QStringLiteral(":text"), merged.textData);
    update.bindValue(QStringLiteral(":blob"), merged.blobData.isNull()
                                                   ? QVariant()
                                                   : QVariant(merged.blobData));
    update.bindValue(QStringLiteral(":preview"), merged.preview);
    update.bindValue(QStringLiteral(":size"), merged.sizeBytes);
    update.bindValue(QStringLiteral(":pinned"), merged.pinned ? 1 : 0);
    update.bindValue(QStringLiteral(":sensitive"), merged.sensitive ? 1 : 0);
    update.bindValue(QStringLiteral(":uses"), merged.useCount);
    update.bindValue(QStringLiteral(":app"), merged.sourceApp);
    update.bindValue(QStringLiteral(":window"), merged.sourceWindow);
    update.bindValue(QStringLiteral(":ocr"), merged.ocrText);
    update.bindValue(QStringLiteral(":id"), keepId);
    if (!update.exec()) {
        qWarning("egoboard: cannot normalize merged legacy entry %lld: %s",
                 keepId, qPrintable(update.lastError().text()));
        return false;
    }
    return true;
}

} // namespace

static bool columnExists(QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery q(db);
    // Identifiers cannot be bound; only the internal "entries" table is ever
    // queried. Quote defensively so a future caller cannot inject SQL here.
    if (table != QLatin1String("entries"))
        return false;
    const QString quoted = QStringLiteral("\"%1\"").arg(
        QString(table).replace(QLatin1Char('"'), QStringLiteral("\"\"")));
    // Use PRAGMA table_info which is stable; pragma_table_info table-valued function
    // does not reliably support bound parameters for the table name in all SQLite builds.
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(quoted))) return false;
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

static QString keyBlobLiteral(const QString &key)
{
    // PRAGMA key/rekey parameters cannot be bound, so avoid string quoting
    // entirely: pass the UTF-8 bytes as a hex blob literal x'...'. Hex has no
    // quoting/escaping surface (no '', ;, --, NUL-truncation ambiguity).
    return QStringLiteral("x'%1'").arg(
        QString::fromLatin1(key.toUtf8().toHex()));
}

bool setKey(QSqlDatabase &db, const QString &key)
{
    if (key.isEmpty())
        return false;
    QSqlQuery q(db);
    const QString sql = QStringLiteral("PRAGMA key = %1").arg(keyBlobLiteral(key));
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
        sql = QStringLiteral("PRAGMA rekey = x''");
    else
        sql = QStringLiteral("PRAGMA rekey = %1").arg(keyBlobLiteral(newKey));
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
        // Deleted secrets must not linger in the freelist for forensic reads.
        QStringLiteral("PRAGMA secure_delete=ON"),
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

    // Older builds stored the top-level sentinel as 0. Keep one representation
    // (NULL) so sibling checks and hierarchy reconstruction agree.
    {
        QSqlQuery normalizeParents(db);
        if (!normalizeParents.exec(QStringLiteral(
                "UPDATE groups SET parent_id = NULL WHERE parent_id = 0"))) {
            qWarning("egoboard: top-level group normalization failed: %s",
                     qPrintable(normalizeParents.lastError().text()));
            return false;
        }
    }

    // The legacy-hash merge reads and preserves OCR data. Add the column before
    // that migration; ensureFts() below also calls this for normal startup.
    if (!columnExists(db, QStringLiteral("entries"), QStringLiteral("ocr_text"))) {
        QSqlQuery alter(db);
        if (!alter.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN ocr_text TEXT"))) {
            qWarning("egoboard: cannot add OCR column before hash migration: %s",
                     qPrintable(alter.lastError().text()));
            return false;
        }
    }

    // content_hash is declared TEXT but older builds bound it as BLOB. SQLite
    // compares values by storage class, so mixed rows miss dedup hits. Merge
    // collisions before normalization so legacy and current rows do not remain
    // as duplicate history entries.
    {
        struct LegacyHash {
            qint64 id = 0;
            QString normalized;
        };
        QList<LegacyHash> legacyRows;
        QSqlQuery select(db);
        if (!select.exec(QStringLiteral(
                "SELECT id, CAST(content_hash AS TEXT) FROM entries"
                " WHERE typeof(content_hash) = 'blob'"))) {
            qWarning("egoboard: cannot read legacy content hashes: %s",
                     qPrintable(select.lastError().text()));
            return false;
        }
        while (select.next())
            legacyRows.append({select.value(0).toLongLong(), select.value(1).toString()});
        select.finish();

        if (!legacyRows.isEmpty()) {
            if (!db.transaction()) {
                qWarning("egoboard: cannot begin legacy hash migration: %s",
                         qPrintable(db.lastError().text()));
                return false;
            }
            bool ok = true;
            for (const LegacyHash &legacy : legacyRows) {
                QSqlQuery duplicate(db);
                duplicate.prepare(QStringLiteral(
                    "SELECT id FROM entries WHERE content_hash = :hash"
                    " AND typeof(content_hash) = 'text' AND id != :id LIMIT 1"));
                duplicate.bindValue(QStringLiteral(":hash"), legacy.normalized);
                duplicate.bindValue(QStringLiteral(":id"), legacy.id);
                if (!duplicate.exec()) {
                    qWarning("egoboard: cannot find legacy hash collision %lld: %s",
                             legacy.id, qPrintable(duplicate.lastError().text()));
                    ok = false;
                    break;
                }
                const bool hasDuplicate = duplicate.next();
                const qint64 duplicateId = hasDuplicate ? duplicate.value(0).toLongLong() : 0;
                duplicate.finish();
                if (hasDuplicate) {
                    const qint64 keepId = qMin(legacy.id, duplicateId);
                    const qint64 dropId = qMax(legacy.id, duplicateId);
                    if (!mergeEntries(db, keepId, dropId, legacy.normalized)) {
                        ok = false;
                        break;
                    }
                    continue;
                }

                QSqlQuery normalize(db);
                normalize.prepare(QStringLiteral(
                    "UPDATE entries SET content_hash = :hash WHERE id = :id"));
                normalize.bindValue(QStringLiteral(":hash"), legacy.normalized);
                normalize.bindValue(QStringLiteral(":id"), legacy.id);
                if (!normalize.exec()) {
                    qWarning("egoboard: content_hash normalization failed for %lld: %s",
                             legacy.id, qPrintable(normalize.lastError().text()));
                    ok = false;
                    break;
                }
            }
            if (!ok || !db.commit()) {
                if (ok)
                    qWarning("egoboard: cannot commit legacy hash migration: %s",
                             qPrintable(db.lastError().text()));
                db.rollback();
                return false;
            }
        }
    }

    ensureFts(db);
    return true;
}

} // namespace DatabaseSchema
