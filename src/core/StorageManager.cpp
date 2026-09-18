#include "StorageManager.h"

#include "DatabaseSchema.h"
#include "SearchEngine.h"

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include <atomic>

namespace {

// Regular-expression search is a guarded fallback: a page fetch scans at most
// this many rows (in small batches, so payloads never arrive in one huge query)
// and matches at most this many characters per field. Together these bound the
// work a pathological pattern can cause on the GUI thread.
constexpr int kRegexScanCap = 20000;
constexpr int kRegexBatch = 200;
constexpr int kRegexMaxSubjectChars = 20000;

bool matchesRegex(const ClipboardRecord &row, const QRegularExpression &regex,
                  FilterSpec::SearchScope scope)
{
    const auto hit = [&regex](const QString &text) {
        if (text.isEmpty())
            return false;
        return regex
            .match(text.size() > kRegexMaxSubjectChars ? text.left(kRegexMaxSubjectChars) : text)
            .hasMatch();
    };
    switch (scope) {
    case FilterSpec::SearchScope::Preview:
        return hit(row.preview);
    case FilterSpec::SearchScope::FullText:
        return hit(row.textData);
    case FilterSpec::SearchScope::Ocr:
        return hit(row.ocrText);
    case FilterSpec::SearchScope::All:
    default:
        return hit(row.preview) || hit(row.textData) || hit(row.ocrText);
    }
}

// SQLCipher databases begin with a random salt, not SQLite's plaintext magic;
// a file without that header can only be read after PRAGMA key.
bool fileLooksEncrypted(const QString &path)
{
    QFile file(path);
    if (!file.exists() || file.size() < 16)
        return false;
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(16);
    return header.size() == 16 && !header.startsWith(QByteArrayLiteral("SQLite format 3"));
}

} // namespace

StorageManager::StorageManager(const QString &databasePath, QObject *parent)
    : IClipboardStorage(parent)
    , m_path(databasePath)
{
    static std::atomic_int connectionCounter{0};
    m_connectionName =
        QStringLiteral("egoboard-history-%1").arg(connectionCounter.fetch_add(1));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(databasePath);
    if (!m_db.open()) {
        qWarning("egoboard: cannot open database %s: %s", qPrintable(databasePath),
                 qPrintable(m_db.lastError().text()));
        return;
    }
    if (fileLooksEncrypted(databasePath)) {
        // The schema cannot be touched before the key is applied; setEncryptionKey()
        // runs it once the database is readable.
        return;
    }
    DatabaseSchema::ensure(m_db);
}

StorageManager::~StorageManager()
{
    const QString connectionName = m_connectionName;
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase(); // release the handle before removing the connection
    QSqlDatabase::removeDatabase(connectionName);
}

qint64 StorageManager::databaseFileSize() const
{
    return QFile::exists(m_path) ? QFile(m_path).size() : 0;
}

bool StorageManager::beginTransaction()
{
    if (m_transactionDepth > 0) {
        ++m_transactionDepth; // join the open transaction (SQLite does not nest)
        return true;
    }
    if (!m_db.transaction())
        return false;
    m_transactionDepth = 1;
    return true;
}

bool StorageManager::commitTransaction()
{
    if (m_transactionDepth <= 0)
        return false;
    if (--m_transactionDepth > 0)
        return true; // the outermost scope still owns the transaction
    return m_db.commit();
}

void StorageManager::rollbackTransaction()
{
    if (m_transactionDepth <= 0)
        return;
    m_transactionDepth = 0; // a failure discards the whole (possibly nested) unit
    m_db.rollback();
}

bool StorageManager::beginBulk()
{
    if (m_bulkDepth > 0) {
        ++m_bulkDepth;
        return true;
    }
    if (!beginTransaction())
        return false;
    m_bulkDepth = 1;
    return true;
}

bool StorageManager::endBulk(bool commit)
{
    if (m_bulkDepth <= 0)
        return false;
    if (--m_bulkDepth > 0)
        return true;
    if (!commit) {
        rollbackTransaction();
        return true;
    }
    return commitTransaction();
}

qint64 StorageManager::insertOrUpdate(const ClipboardRecord &record, bool *updatedExisting)
{
    if (updatedExisting)
        *updatedExisting = false;
    if (!m_db.isOpen())
        return 0;

    if (!beginTransaction()) {
        qWarning("egoboard: cannot begin transaction: %s", qPrintable(m_db.lastError().text()));
        return 0;
    }

    {
        QSqlQuery find(m_db);
        find.prepare(QStringLiteral("SELECT id FROM entries WHERE content_hash = :h LIMIT 1"));
        // The column is TEXT; hashes are hex, so bind them as text to match
        // both older normalized rows and new ones (BLOB binds would not).
        find.bindValue(QStringLiteral(":h"), QString::fromLatin1(record.hash));
        if (!find.exec()) {
            qWarning("egoboard: duplicate lookup failed: %s", qPrintable(find.lastError().text()));
            rollbackTransaction();
            return 0;
        }
        if (find.next()) {
            const qint64 existingId = find.value(0).toLongLong();
            // Dedup touch: the latest copy wins for payload and metadata, but
            // the timestamp only moves forward - copying an old entry again
            // must not sink it in Newest order. A pin is never removed and
            // existing OCR text survives a copy that carries none.
            QSqlQuery touch(m_db);
            touch.prepare(QStringLiteral(
                "UPDATE entries SET"
                " timestamp_ms = MAX(timestamp_ms, :ts),"
                " use_count = use_count + 1,"
                " source_app = :app, source_window = :win,"
                " preview = :preview, size_bytes = :size,"
                " text_data = :text, blob_data = :blob,"
                " sensitive = :sensitive,"
                " pinned = MAX(pinned, :pinned),"
                " ocr_text = CASE WHEN :ocr != '' THEN :ocr ELSE ocr_text END"
                " WHERE id = :id"));
            touch.bindValue(QStringLiteral(":ts"), record.timestamp);
            touch.bindValue(QStringLiteral(":app"), record.sourceApp);
            touch.bindValue(QStringLiteral(":win"), record.sourceWindow);
            touch.bindValue(QStringLiteral(":preview"), record.preview);
            touch.bindValue(QStringLiteral(":size"), record.sizeBytes);
            touch.bindValue(QStringLiteral(":text"), record.textData);
            touch.bindValue(QStringLiteral(":blob"), record.hasBlob ? record.blobData : QVariant());
            touch.bindValue(QStringLiteral(":sensitive"), record.sensitive ? 1 : 0);
            touch.bindValue(QStringLiteral(":pinned"), record.pinned ? 1 : 0);
            touch.bindValue(QStringLiteral(":ocr"), record.ocrText);
            touch.bindValue(QStringLiteral(":id"), existingId);
            if (!touch.exec() || !commitTransaction()) {
                qWarning("egoboard: duplicate update failed: %s",
                         qPrintable(touch.lastError().text()));
                rollbackTransaction();
                return 0;
            }
            if (updatedExisting)
                *updatedExisting = true;
            if (!signalsSuppressed())
                emit entryTouched(existingId);
            return existingId;
        }
    }

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO entries (timestamp_ms, content_type, content_hash, text_data, blob_data,"
        " preview, size_bytes, pinned, sensitive, use_count, source_app, source_window)"
        " VALUES (:ts, :type, :h, :text, :blob, :preview, :size, :pinned, :sensitive, 0, :app, :win)"));
    insert.bindValue(QStringLiteral(":ts"), record.timestamp);
    insert.bindValue(QStringLiteral(":type"), static_cast<int>(record.type));
    insert.bindValue(QStringLiteral(":h"), QString::fromLatin1(record.hash));
    insert.bindValue(QStringLiteral(":text"), record.textData);
    insert.bindValue(QStringLiteral(":blob"), record.hasBlob ? record.blobData : QVariant());
    insert.bindValue(QStringLiteral(":preview"), record.preview);
    insert.bindValue(QStringLiteral(":size"), record.sizeBytes);
    insert.bindValue(QStringLiteral(":pinned"), record.pinned ? 1 : 0);
    insert.bindValue(QStringLiteral(":sensitive"), record.sensitive ? 1 : 0);
    insert.bindValue(QStringLiteral(":app"), record.sourceApp);
    insert.bindValue(QStringLiteral(":win"), record.sourceWindow);
    if (!insert.exec()) {
        qWarning("egoboard: insert failed: %s", qPrintable(insert.lastError().text()));
        rollbackTransaction();
        return 0;
    }
    if (!commitTransaction()) {
        qWarning("egoboard: insert commit failed: %s", qPrintable(m_db.lastError().text()));
        rollbackTransaction();
        return 0;
    }

    const qint64 id = insert.lastInsertId().toLongLong();
    if (!signalsSuppressed())
        emit entryAdded(id);
    return id;
}

QVector<ClipboardRecord> StorageManager::fetchPage(const FilterSpec &filter, const PageCursor &cursor,
                                                   int limit, bool *hasMore) const
{
    if (hasMore)
        *hasMore = false;
    if (!m_db.isOpen() || limit <= 0)
        return {};
    if (filter.regexText.isEmpty())
        return fetchPageSql(filter, cursor, limit, hasMore, false);
    return fetchPageRegex(filter, cursor, limit, hasMore);
}

QVector<ClipboardRecord> StorageManager::fetchPageRegex(const FilterSpec &filter,
                                                        const PageCursor &cursor, int limit,
                                                        bool *hasMore) const
{
    QRegularExpression regex(filter.regexText);
    if (!regex.isValid())
        return {};

    FilterSpec plain = filter;
    plain.regexText.clear();

    QVector<ClipboardRecord> matches;
    matches.reserve(limit);
    PageCursor scan = cursor;
    int scanned = 0;
    while (scanned < kRegexScanCap) {
        bool batchHasMore = false;
        const int batchSize = qMin(kRegexBatch, kRegexScanCap - scanned);
        const QVector<ClipboardRecord> batch =
            fetchPageSql(plain, scan, batchSize, &batchHasMore, true);
        if (batch.isEmpty())
            break;
        scanned += batch.size();
        for (const ClipboardRecord &row : batch) {
            if (!matchesRegex(row, regex, filter.searchScope))
                continue;
            if (matches.size() == limit) {
                if (hasMore)
                    *hasMore = true; // found one match past the page
                return matches;
            }
            matches.append(row);
        }
        if (!batchHasMore)
            break;
        const ClipboardRecord &last = batch.constLast();
        scan = PageCursor{true, last.timestamp, last.id, last.useCount};
    }
    return matches;
}

QVector<ClipboardRecord> StorageManager::fetchPageSql(const FilterSpec &filter,
                                                      const PageCursor &cursor, int limit,
                                                      bool *hasMore, bool includeText) const
{
    QVector<ClipboardRecord> results;
    if (hasMore)
        *hasMore = false;
    if (!m_db.isOpen() || limit <= 0)
        return results;

    QStringList where;
    QHash<QString, QVariant> binds; // unique named placeholders :w0, :w1, ...
    int bindIndex = 0;
    const auto addBind = [&binds, &bindIndex](const QVariant &value) {
        const QString name = QStringLiteral(":w%1").arg(bindIndex++);
        binds.insert(name, value);
        return name;
    };

    const bool hasTextQuery = !filter.searchText.isEmpty() || !filter.excludeText.isEmpty();
    const bool ftsAvailable = hasTextQuery && SearchEngine::isFtsAvailable(m_db);

    // LIKE fallback for one term, restricted to the active search scope.
    const auto likeMatch = [&](const QString &placeholder) -> QString {
        switch (filter.searchScope) {
        case FilterSpec::SearchScope::Preview:
            return QStringLiteral("preview LIKE %1 ESCAPE '\\'").arg(placeholder);
        case FilterSpec::SearchScope::FullText:
            return QStringLiteral("text_data LIKE %1 ESCAPE '\\'").arg(placeholder);
        case FilterSpec::SearchScope::Ocr:
            return QStringLiteral("ocr_text LIKE %1 ESCAPE '\\'").arg(placeholder);
        case FilterSpec::SearchScope::All:
        default:
            return QStringLiteral("(preview LIKE %1 ESCAPE '\\' OR text_data LIKE %1 ESCAPE '\\' OR ocr_text LIKE %1 ESCAPE '\\')")
                .arg(placeholder);
        }
    };

    if (!filter.searchText.isEmpty()) {
        const QString ftsQuery = SearchEngine::buildFtsQuery(filter.searchText, filter.searchScope);
        if (!ftsQuery.isEmpty() && ftsAvailable) {
            const QString placeholder = addBind(ftsQuery);
            where << QStringLiteral("id IN (SELECT rowid FROM entries_fts WHERE entries_fts MATCH %1)")
                         .arg(placeholder);
        } else {
            // OR-groups: alternatives of AND terms ("a b OR c").
            QStringList orParts;
            for (const QStringList &group : SearchEngine::orGroups(filter.searchText)) {
                QStringList andParts;
                for (const QString &term : group) {
                    const QString needle =
                        QStringLiteral("%") + SearchEngine::likeEscape(term) + QStringLiteral("%");
                    andParts << likeMatch(addBind(needle));
                }
                if (!andParts.isEmpty())
                    orParts << QStringLiteral("(%1)").arg(andParts.join(QStringLiteral(" AND ")));
            }
            if (!orParts.isEmpty())
                where << QStringLiteral("(%1)").arg(orParts.join(QStringLiteral(" OR ")));
        }
    }
    if (!filter.excludeText.isEmpty()) {
        // Every "-term" / NOT term excludes on its own; a match anywhere drops
        // the entry, regardless of the search scope.
        for (const QStringList &group : SearchEngine::orGroups(filter.excludeText)) {
            for (const QString &term : group) {
                const QString ftsTerm = SearchEngine::buildFtsTerm(term);
                if (!ftsTerm.isEmpty() && ftsAvailable) {
                    const QString placeholder = addBind(ftsTerm);
                    where << QStringLiteral("id NOT IN (SELECT rowid FROM entries_fts WHERE entries_fts MATCH %1)")
                                 .arg(placeholder);
                    continue;
                }
                const QString needle =
                    QStringLiteral("%") + SearchEngine::likeEscape(term) + QStringLiteral("%");
                const QString placeholder = addBind(needle);
                // COALESCE: NULL NOT LIKE x is NULL, which would drop every row.
                where << QStringLiteral(
                             "(COALESCE(preview, '') NOT LIKE %1 ESCAPE '\\'"
                             " AND COALESCE(text_data, '') NOT LIKE %1 ESCAPE '\\'"
                             " AND COALESCE(ocr_text, '') NOT LIKE %1 ESCAPE '\\')")
                             .arg(placeholder);
            }
        }
    }
    if (filter.contentType >= 0)
        where << QStringLiteral("content_type = %1").arg(addBind(filter.contentType));
    if (filter.fromMs > 0)
        where << QStringLiteral("timestamp_ms >= %1").arg(addBind(filter.fromMs));
    if (filter.toMs > 0)
        where << QStringLiteral("timestamp_ms <= %1").arg(addBind(filter.toMs));
    if (!filter.sourceApp.isEmpty())
        where << QStringLiteral("source_app = %1").arg(addBind(filter.sourceApp));
    if (filter.groupId.has_value())
        where << QStringLiteral("id IN (SELECT entry_id FROM entry_groups WHERE group_id = %1)")
                     .arg(addBind(filter.groupId.value()));
    if (filter.pinnedOnly)
        where << QStringLiteral("pinned = 1");
    if (filter.sensitiveOnly)
        where << QStringLiteral("sensitive = 1");
    if (filter.hasOcrOnly)
        where << QStringLiteral("(ocr_text IS NOT NULL AND ocr_text <> '')");
    // Entry must carry EVERY tag in the filter (AND semantics via subqueries).
    for (const QString &tag : filter.tags) {
        const QString placeholder = addBind(tag);
        where << QStringLiteral(
            "id IN (SELECT et.entry_id FROM entry_tags et"
            " JOIN tags t ON t.id = et.tag_id WHERE t.name = %1 COLLATE NOCASE)")
                 .arg(placeholder);
    }
    if (cursor.valid) {
        // Keyset pagination condition follows the active sort order so pages
        // stay stable whatever the mode.
        switch (filter.sortMode) {
        case FilterSpec::SortMode::Oldest: {
            const QString tsPlaceholder = addBind(cursor.timestampMs);
            const QString idPlaceholder = addBind(cursor.id);
            where << QStringLiteral("(timestamp_ms > %1 OR (timestamp_ms = %1 AND id > %2))")
                         .arg(tsPlaceholder, idPlaceholder);
            break;
        }
        case FilterSpec::SortMode::MostUsed: {
            const QString ucPlaceholder = addBind(cursor.useCount);
            const QString tsPlaceholder = addBind(cursor.timestampMs);
            const QString idPlaceholder = addBind(cursor.id);
            where << QStringLiteral(
                             "(use_count < %1 OR (use_count = %1 AND"
                             " (timestamp_ms < %2 OR (timestamp_ms = %2 AND id < %3))))")
                             .arg(ucPlaceholder, tsPlaceholder, idPlaceholder);
            break;
        }
        case FilterSpec::SortMode::Newest:
        default: {
            const QString tsPlaceholder = addBind(cursor.timestampMs);
            const QString idPlaceholder = addBind(cursor.id);
            where << QStringLiteral("(timestamp_ms < %1 OR (timestamp_ms = %1 AND id < %2))")
                         .arg(tsPlaceholder, idPlaceholder);
            break;
        }
        }
    }

    QString orderBy;
    switch (filter.sortMode) {
    case FilterSpec::SortMode::Oldest:
        orderBy = QStringLiteral(" ORDER BY timestamp_ms ASC, id ASC LIMIT :lim");
        break;
    case FilterSpec::SortMode::MostUsed:
        orderBy = QStringLiteral(" ORDER BY use_count DESC, timestamp_ms DESC, id DESC LIMIT :lim");
        break;
    case FilterSpec::SortMode::Newest:
    default:
        orderBy = QStringLiteral(" ORDER BY timestamp_ms DESC, id DESC LIMIT :lim");
        break;
    }
    QString sql = QStringLiteral(
        "SELECT id, timestamp_ms, content_type, content_hash, preview, size_bytes, pinned,"
        " sensitive, use_count, source_app, source_window,"
        " (blob_data IS NOT NULL AND LENGTH(blob_data) > 0) AS has_blob");
    if (includeText)
        sql += QStringLiteral(", text_data, ocr_text"); // regex verification
    sql += QStringLiteral(" FROM entries");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += orderBy;

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (auto it = binds.cbegin(); it != binds.cend(); ++it)
        query.bindValue(it.key(), it.value());
    query.bindValue(QStringLiteral(":lim"), limit + 1);

    if (!query.exec()) {
        qWarning("egoboard: fetchPage failed: %s", qPrintable(query.lastError().text()));
        return results;
    }
    while (query.next()) {
        if (results.size() == limit) {
            if (hasMore)
                *hasMore = true;
            break;
        }
        ClipboardRecord record = recordFromSummary(query);
        if (includeText) {
            record.textData = query.value(12).toString();
            record.ocrText = query.value(13).toString();
        }
        results.append(record);
    }
    return results;
}

QVector<ClipboardRecord> StorageManager::fetchAll(const FilterSpec &filter) const
{
    QVector<ClipboardRecord> all;
    PageCursor cursor;
    static constexpr int kPageSize = 500;
    while (true) {
        bool hasMore = false;
        const auto page = fetchPage(filter, cursor, kPageSize, &hasMore);
        if (page.isEmpty())
            break;
        all.append(page);
        if (!hasMore)
            break;
        // The cursor must carry every key the active sort compares, otherwise
        // paging past page 1 silently truncates (MostUsed also orders by
        // use_count before timestamp/id).
        cursor = PageCursor{true, page.last().timestamp, page.last().id, page.last().useCount};
    }
    return all;
}

QVector<ClipboardRecord> StorageManager::fetchAllFull(const FilterSpec &filter) const
{
    QVector<ClipboardRecord> full;
    PageCursor cursor;
    static constexpr int kPageSize = 500;
    while (true) {
        bool hasMore = false;
        const auto page = fetchPage(filter, cursor, kPageSize, &hasMore);
        if (page.isEmpty())
            break;

        // The payloads of one page are fetched in a single query; doing it per
        // entry made the export N+1 and dominated its runtime on large histories.
        QStringList placeholders;
        placeholders.reserve(page.size());
        for (int i = 0; i < page.size(); ++i)
            placeholders << QStringLiteral("?");
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT id, timestamp_ms, content_type, content_hash, text_data, blob_data, preview,"
            " size_bytes, pinned, sensitive, use_count, source_app, source_window, ocr_text"
            " FROM entries WHERE id IN (%1)").arg(placeholders.join(QLatin1Char(','))));
        for (int i = 0; i < page.size(); ++i)
            query.bindValue(i, page.at(i).id);
        if (!query.exec()) {
            qWarning("egoboard: fetchAllFull page failed: %s", qPrintable(query.lastError().text()));
            break;
        }
        QHash<qint64, ClipboardRecord> byId;
        byId.reserve(page.size());
        while (query.next()) {
            ClipboardRecord record = recordFromFull(query);
            byId.insert(record.id, record);
        }
        for (const ClipboardRecord &summary : page) {
            const auto it = byId.constFind(summary.id);
            if (it != byId.constEnd())
                full.append(*it);
        }

        if (!hasMore)
            break;
        cursor = PageCursor{true, page.last().timestamp, page.last().id, page.last().useCount};
    }
    return full;
}

ClipboardRecord StorageManager::recordFromSummary(const QSqlQuery &query)
{
    ClipboardRecord record;
    record.id = query.value(0).toLongLong();
    record.timestamp = query.value(1).toLongLong();
    record.type = static_cast<ContentType>(query.value(2).toInt());
    record.hash = query.value(3).toByteArray();
    record.preview = query.value(4).toString();
    record.sizeBytes = query.value(5).toLongLong();
    record.pinned = query.value(6).toInt() != 0;
    record.sensitive = query.value(7).toInt() != 0;
    record.useCount = query.value(8).toInt();
    record.sourceApp = query.value(9).toString();
    record.sourceWindow = query.value(10).toString();
    record.hasBlob = query.value(11).toInt() != 0;
    return record;
}

ClipboardRecord StorageManager::recordFromFull(const QSqlQuery &query)
{
    ClipboardRecord record;
    record.id = query.value(0).toLongLong();
    record.timestamp = query.value(1).toLongLong();
    record.type = static_cast<ContentType>(query.value(2).toInt());
    record.hash = query.value(3).toByteArray();
    record.textData = query.value(4).toString();
    record.blobData = query.value(5).toByteArray();
    record.hasBlob = !record.blobData.isEmpty();
    record.preview = query.value(6).toString();
    record.sizeBytes = query.value(7).toLongLong();
    record.pinned = query.value(8).toInt() != 0;
    record.sensitive = query.value(9).toInt() != 0;
    record.useCount = query.value(10).toInt();
    record.sourceApp = query.value(11).toString();
    record.sourceWindow = query.value(12).toString();
    record.ocrText = query.value(13).toString();
    return record;
}

bool StorageManager::fetchFull(qint64 id, ClipboardRecord *out) const
{
    if (!m_db.isOpen() || !out)
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, timestamp_ms, content_type, content_hash, text_data, blob_data, preview,"
        " size_bytes, pinned, sensitive, use_count, source_app, source_window, ocr_text"
        " FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next()) {
        if (!query.isValid())
            qWarning("egoboard: fetchFull(%lld) failed: %s", id, qPrintable(query.lastError().text()));
        return false;
    }
    *out = recordFromFull(query);
    return true;
}

bool StorageManager::fetchSummary(qint64 id, ClipboardRecord *out) const
{
    if (!m_db.isOpen() || !out || id <= 0)
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, timestamp_ms, content_type, content_hash, preview, size_bytes, pinned,"
        " sensitive, use_count, source_app, source_window,"
        " (blob_data IS NOT NULL AND LENGTH(blob_data) > 0) AS has_blob"
        " FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return false;
    *out = recordFromSummary(query);
    return true;
}

bool StorageManager::remove(qint64 id)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return false;
    const int removed = query.numRowsAffected();
    if (removed > 0 && !signalsSuppressed())
        emit entriesRemoved({id});
    return removed > 0;
}

int StorageManager::removeEntries(const QList<qint64> &ids)
{
    if (!m_db.isOpen() || ids.isEmpty())
        return 0;
    if (!beginTransaction()) {
        qWarning("egoboard: cannot begin transaction: %s", qPrintable(m_db.lastError().text()));
        return 0;
    }
    QList<qint64> removedIds;
    removedIds.reserve(ids.size());
    for (const qint64 id : ids) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("DELETE FROM entries WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), id);
        if (query.exec() && query.numRowsAffected() > 0)
            removedIds.append(id); // only rows that actually existed
    }
    if (!commitTransaction()) {
        qWarning("egoboard: removeEntries commit failed: %s",
                 qPrintable(m_db.lastError().text()));
        rollbackTransaction();
        return 0;
    }
    if (!removedIds.isEmpty() && !signalsSuppressed())
        emit entriesRemoved(removedIds);
    return removedIds.size();
}

int StorageManager::clearHistory(bool includePinned)
{
    if (!m_db.isOpen())
        return 0;
    // Collect the doomed ids first so listeners can react to exactly what was
    // removed instead of inferring it from the reset.
    const QString where = includePinned ? QString() : QStringLiteral(" WHERE pinned = 0");
    QList<qint64> removedIds;
    {
        QSqlQuery select(m_db);
        if (!select.exec(QStringLiteral("SELECT id FROM entries") + where)) {
            qWarning("egoboard: clearHistory select failed: %s",
                     qPrintable(select.lastError().text()));
            return 0;
        }
        while (select.next())
            removedIds.append(select.value(0).toLongLong());
    }
    if (removedIds.isEmpty())
        return 0;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM entries") + where)) {
        qWarning("egoboard: clearHistory failed: %s", qPrintable(query.lastError().text()));
        return 0;
    }
    if (!signalsSuppressed()) {
        emit entriesRemoved(removedIds);
        emit storageReset();
    }
    return removedIds.size();
}

bool StorageManager::setPinned(qint64 id, bool pinned)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE entries SET pinned = :p WHERE id = :id"));
    query.bindValue(QStringLiteral(":p"), pinned ? 1 : 0);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || query.numRowsAffected() == 0)
        return false;
    if (!signalsSuppressed())
        emit pinnedChanged(id, pinned);
    return true;
}

bool StorageManager::setOcrText(qint64 id, const QString &ocrText)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE entries SET ocr_text = :t WHERE id = :id"));
    query.bindValue(QStringLiteral(":t"), ocrText);
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec() && query.numRowsAffected() > 0;
}

bool StorageManager::touchEntry(qint64 id)
{
    if (!m_db.isOpen() || id <= 0)
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE entries SET timestamp_ms = :ts, use_count = use_count + 1 WHERE id = :id"));
    query.bindValue(QStringLiteral(":ts"), QDateTime::currentMSecsSinceEpoch());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || query.numRowsAffected() != 1) {
        qWarning("egoboard: touchEntry failed: %s", qPrintable(query.lastError().text()));
        return false;
    }
    if (!signalsSuppressed())
        emit entryTouched(id);
    return true;
}

QStringList StorageManager::allTags() const
{
    QStringList tags;
    if (!m_db.isOpen())
        return tags;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT name FROM tags ORDER BY name COLLATE NOCASE")))
        return tags;
    while (query.next())
        tags.append(query.value(0).toString());
    return tags;
}

QStringList StorageManager::tagsForEntry(qint64 entryId) const
{
    QStringList tags;
    if (!m_db.isOpen() || entryId <= 0)
        return tags;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT t.name FROM tags t JOIN entry_tags et ON et.tag_id = t.id"
        " WHERE et.entry_id = :id ORDER BY t.name COLLATE NOCASE"));
    query.bindValue(QStringLiteral(":id"), entryId);
    if (!query.exec())
        return tags;
    while (query.next())
        tags.append(query.value(0).toString());
    return tags;
}

bool StorageManager::addTag(qint64 entryId, const QString &tag)
{
    if (!m_db.isOpen() || entryId <= 0 || tag.trimmed().isEmpty())
        return false;
    const QString name = tag.trimmed();

    if (!beginTransaction())
        return false;
    QSqlQuery insertTag(m_db);
    insertTag.prepare(QStringLiteral("INSERT OR IGNORE INTO tags (name) VALUES (:name)"));
    insertTag.bindValue(QStringLiteral(":name"), name);
    if (!insertTag.exec()) {
        qWarning("egoboard: addTag failed: %s", qPrintable(insertTag.lastError().text()));
        rollbackTransaction();
        return false;
    }
    QSqlQuery link(m_db);
    link.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO entry_tags (entry_id, tag_id)"
        " VALUES (:id, (SELECT id FROM tags WHERE name = :name COLLATE NOCASE))"));
    link.bindValue(QStringLiteral(":id"), entryId);
    link.bindValue(QStringLiteral(":name"), name);
    if (!link.exec() || !commitTransaction()) {
        qWarning("egoboard: addTag link failed: %s", qPrintable(link.lastError().text()));
        rollbackTransaction();
        return false;
    }
    return true;
}

bool StorageManager::removeTag(qint64 entryId, const QString &tag)
{
    if (!m_db.isOpen() || entryId <= 0 || tag.trimmed().isEmpty())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM entry_tags WHERE entry_id = :id"
        " AND tag_id = (SELECT id FROM tags WHERE name = :name COLLATE NOCASE)"));
    query.bindValue(QStringLiteral(":id"), entryId);
    query.bindValue(QStringLiteral(":name"), tag.trimmed());
    if (!query.exec()) {
        qWarning("egoboard: removeTag failed: %s", qPrintable(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() <= 0)
        return false; // the entry did not carry the tag
    // Tags nobody uses anymore disappear from the list.
    QSqlQuery prune(m_db);
    prune.exec(QStringLiteral(
        "DELETE FROM tags WHERE id NOT IN (SELECT DISTINCT tag_id FROM entry_tags)"));
    return true;
}

QList<SavedSearch> StorageManager::savedSearches() const
{
    QList<SavedSearch> searches;
    if (!m_db.isOpen())
        return searches;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, name, filter FROM saved_searches ORDER BY name COLLATE NOCASE")))
        return searches;
    while (query.next()) {
        SavedSearch search;
        search.id = query.value(0).toLongLong();
        search.name = query.value(1).toString();
        search.filter = FilterSpec::fromJsonString(query.value(2).toString());
        searches.append(search);
    }
    return searches;
}

qint64 StorageManager::addSavedSearch(const QString &name, const FilterSpec &filter)
{
    if (!m_db.isOpen() || name.trimmed().isEmpty())
        return 0;
    const QString trimmed = name.trimmed();

    // True upsert on the case-insensitive name: an existing search keeps its
    // row id, only its filter is replaced.
    QSqlQuery find(m_db);
    find.prepare(QStringLiteral(
        "SELECT id FROM saved_searches WHERE name = :name COLLATE NOCASE LIMIT 1"));
    find.bindValue(QStringLiteral(":name"), trimmed);
    if (!find.exec()) {
        qWarning("egoboard: addSavedSearch lookup failed: %s",
                 qPrintable(find.lastError().text()));
        return 0;
    }
    if (find.next()) {
        const qint64 id = find.value(0).toLongLong();
        QSqlQuery update(m_db);
        update.prepare(QStringLiteral("UPDATE saved_searches SET filter = :filter WHERE id = :id"));
        update.bindValue(QStringLiteral(":filter"), filter.toJsonString());
        update.bindValue(QStringLiteral(":id"), id);
        if (!update.exec()) {
            qWarning("egoboard: addSavedSearch update failed: %s",
                     qPrintable(update.lastError().text()));
            return 0;
        }
        return id;
    }

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO saved_searches (name, filter) VALUES (:name, :filter)"));
    insert.bindValue(QStringLiteral(":name"), trimmed);
    insert.bindValue(QStringLiteral(":filter"), filter.toJsonString());
    if (!insert.exec()) {
        qWarning("egoboard: addSavedSearch failed: %s", qPrintable(insert.lastError().text()));
        return 0;
    }
    return insert.lastInsertId().toLongLong();
}

bool StorageManager::removeSavedSearch(qint64 id)
{
    if (!m_db.isOpen() || id <= 0)
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM saved_searches WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec() && query.numRowsAffected() > 0;
}

QStringList StorageManager::sourceApps() const
{
    QStringList apps;
    if (!m_db.isOpen())
        return apps;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral(
        "SELECT DISTINCT source_app FROM entries WHERE source_app IS NOT NULL AND source_app != ''"
        " ORDER BY source_app COLLATE NOCASE"));
    while (query.next())
        apps.append(query.value(0).toString());
    return apps;
}

StorageStats StorageManager::stats() const
{
    StorageStats stats;
    if (!m_db.isOpen())
        return stats;
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(size_bytes),0) FROM entries"))
        && query.next()) {
        stats.entryCount = query.value(0).toLongLong();
        stats.totalBytes = query.value(1).toLongLong();
    }
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM entries WHERE pinned = 1")) && query.next())
        stats.pinnedCount = query.value(0).toLongLong();
    if (query.exec(
            QStringLiteral("SELECT COUNT(*) FROM entries WHERE content_type = %1")
                .arg(static_cast<int>(ContentType::Image)))
        && query.next())
        stats.imageCount = query.value(0).toLongLong();
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM entries WHERE ocr_text IS NOT NULL AND ocr_text != ''"))
        && query.next())
        stats.ocrCount = query.value(0).toLongLong();
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM entries WHERE sensitive = 1")) && query.next())
        stats.sensitiveCount = query.value(0).toLongLong();
    return stats;
}

int StorageManager::expireEntries(qint64 olderThanMs, int contentType,
                                  const QString &sourceAppWildcard, bool keepPinned)
{
    if (!m_db.isOpen() || olderThanMs <= 0)
        return 0;

    QStringList where;
    QHash<QString, QVariant> binds; // unique named placeholders :w0, :w1, ...
    int bindIndex = 0;
    const auto addBind = [&binds, &bindIndex](const QVariant &value) {
        const QString name = QStringLiteral(":w%1").arg(bindIndex++);
        binds.insert(name, value);
        return name;
    };

    where << QStringLiteral("timestamp_ms < %1").arg(addBind(olderThanMs));
    if (contentType >= 0)
        where << QStringLiteral("content_type = %1").arg(addBind(contentType));
    if (keepPinned)
        where << QStringLiteral("pinned = 0");
    if (!sourceAppWildcard.isEmpty()) {
        // Translate "firefox*" / "org.kde.*" into a LIKE pattern. Escape
        // literal backslash/%/_ FIRST so only the wildcards converted below
        // act as LIKE metacharacters.
        QString pattern = sourceAppWildcard;
        pattern.replace(QStringLiteral("\\"), QStringLiteral("\\\\"))
               .replace(QStringLiteral("%"), QStringLiteral("\\%"))
               .replace(QStringLiteral("_"), QStringLiteral("\\_"));
        pattern.replace(QStringLiteral("*"), QStringLiteral("%"));
        pattern.replace(QStringLiteral("?"), QStringLiteral("_"));
        where << QStringLiteral("source_app LIKE %1 ESCAPE '\\'").arg(addBind(pattern));
    }

    QString sql = QStringLiteral("SELECT id FROM entries");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (auto it = binds.cbegin(); it != binds.cend(); ++it)
        query.bindValue(it.key(), it.value());
    if (!query.exec()) {
        qWarning("egoboard: expireEntries failed: %s", qPrintable(query.lastError().text()));
        return 0;
    }
    QList<qint64> victims;
    while (query.next())
        victims.append(query.value(0).toLongLong());
    return victims.isEmpty() ? 0 : removeEntries(victims);
}

int StorageManager::enforceDiskCap(qint64 maxBytes)
{
    if (!m_db.isOpen() || maxBytes <= 0)
        return 0;
    qint64 total = stats().totalBytes;
    if (total <= maxBytes)
        return 0;

    QList<qint64> victims;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, size_bytes FROM entries WHERE pinned = 0 ORDER BY timestamp_ms ASC, id ASC"));
    if (!query.exec())
        return 0;
    while (query.next() && total > maxBytes) {
        total -= query.value(1).toLongLong();
        victims.append(query.value(0).toLongLong());
    }
    return removeEntries(victims);
}

int StorageManager::enforceMaxEntries(qint64 maxEntries)
{
    if (!m_db.isOpen() || maxEntries <= 0)
        return 0;
    const qint64 count = stats().entryCount;
    if (count <= maxEntries)
        return 0;

    QList<qint64> victims;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id FROM entries WHERE pinned = 0 ORDER BY timestamp_ms ASC, id ASC LIMIT ?"));
    query.bindValue(0, count - maxEntries);
    if (!query.exec())
        return 0;
    while (query.next())
        victims.append(query.value(0).toLongLong());
    return removeEntries(victims);
}

bool StorageManager::setEncryptionKey(const QString &key)
{
    if (key.isEmpty())
        return false;
    if (!isSqlCipherAvailable()) {
        qWarning("egoboard: cannot unlock the database: SQLCipher is not available in this build");
        return false;
    }
    if (!DatabaseSchema::setKey(m_db, key)) {
        qWarning("egoboard: the database key was rejected");
        return false;
    }
    m_encrypted = true;
    // Schema setup was skipped while the file was locked; do it now that it reads.
    return DatabaseSchema::ensure(m_db);
}

bool StorageManager::changeEncryptionKey(const QString &newKey)
{
    if (newKey.isEmpty()) {
        if (!m_encrypted)
            return true; // already plaintext, nothing to decrypt
        if (!DatabaseSchema::rekey(m_db, QString())) {
            qWarning("egoboard: could not decrypt the database");
            return false;
        }
        m_encrypted = false;
        return true;
    }
    if (!isSqlCipherAvailable()) {
        // Plain SQLite ignores PRAGMA rekey and reports success; refuse instead
        // of claiming the database is encrypted when it is not.
        qWarning("egoboard: cannot encrypt the database: SQLCipher is not available in this build");
        return false;
    }
    if (!DatabaseSchema::rekey(m_db, newKey)) {
        qWarning("egoboard: could not encrypt the database");
        return false;
    }
    m_encrypted = true;
    return true;
}

bool StorageManager::verifyEncryptionKey() const
{
    QSqlDatabase db = m_db;
    return DatabaseSchema::probeKey(db);
}

bool StorageManager::requiresEncryptionKey() const
{
    return !m_encrypted && fileLooksEncrypted(m_path);
}

bool StorageManager::isSqlCipherAvailable() const
{
    QSqlDatabase db = m_db;
    return DatabaseSchema::isSqlCipherAvailable(db);
}

QString StorageManager::cipherVersion() const
{
    QSqlDatabase db = m_db;
    return DatabaseSchema::cipherVersion(db);
}
