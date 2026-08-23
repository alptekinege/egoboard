#include "StorageManager.h"

#include "DatabaseSchema.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include <atomic>

namespace {

// Escapes % and _ so user search text is treated literally inside LIKE.
QString likeEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() * 2);
    for (const QChar c : text) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
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

qint64 StorageManager::insertOrUpdate(const ClipboardRecord &record, bool *updatedExisting)
{
    if (updatedExisting)
        *updatedExisting = false;
    if (!m_db.isOpen())
        return 0;

    if (!m_db.transaction()) {
        qWarning("egoboard: cannot begin transaction: %s", qPrintable(m_db.lastError().text()));
        return 0;
    }

    {
        QSqlQuery find(m_db);
        find.prepare(QStringLiteral("SELECT id FROM entries WHERE content_hash = :h LIMIT 1"));
        find.bindValue(QStringLiteral(":h"), record.hash);
        if (!find.exec()) {
            qWarning("egoboard: duplicate lookup failed: %s", qPrintable(find.lastError().text()));
            m_db.rollback();
            return 0;
        }
        if (find.next()) {
            const qint64 existingId = find.value(0).toLongLong();
            QSqlQuery touch(m_db);
            touch.prepare(QStringLiteral(
                "UPDATE entries SET timestamp_ms = :ts, use_count = use_count + 1,"
                " source_app = :app, source_window = :win WHERE id = :id"));
            touch.bindValue(QStringLiteral(":ts"), record.timestamp);
            touch.bindValue(QStringLiteral(":app"), record.sourceApp);
            touch.bindValue(QStringLiteral(":win"), record.sourceWindow);
            touch.bindValue(QStringLiteral(":id"), existingId);
            if (!touch.exec() || !m_db.commit()) {
                qWarning("egoboard: duplicate update failed: %s",
                         qPrintable(touch.lastError().text()));
                m_db.rollback();
                return 0;
            }
            if (updatedExisting)
                *updatedExisting = true;
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
    insert.bindValue(QStringLiteral(":h"), record.hash);
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
        m_db.rollback();
        return 0;
    }
    if (!m_db.commit()) {
        qWarning("egoboard: insert commit failed: %s", qPrintable(m_db.lastError().text()));
        m_db.rollback();
        return 0;
    }

    const qint64 id = insert.lastInsertId().toLongLong();
    emit entryAdded(id);
    return id;
}

QVector<ClipboardRecord> StorageManager::fetchPage(const FilterSpec &filter, const PageCursor &cursor,
                                                   int limit, bool *hasMore) const
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

    if (!filter.searchText.isEmpty()) {
        const QString needle =
            QStringLiteral("%") + likeEscape(filter.searchText) + QStringLiteral("%");
        const QString placeholder = addBind(needle);
        where << QStringLiteral("(preview LIKE %1 ESCAPE '\\' OR text_data LIKE %1 ESCAPE '\\')")
                     .arg(placeholder);
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
    if (cursor.valid) {
        const QString tsPlaceholder = addBind(cursor.timestampMs);
        const QString idPlaceholder = addBind(cursor.id);
        where << QStringLiteral("(timestamp_ms < %1 OR (timestamp_ms = %1 AND id < %2))")
                     .arg(tsPlaceholder, idPlaceholder);
    }

    QString sql = QStringLiteral(
        "SELECT id, timestamp_ms, content_type, content_hash, preview, size_bytes, pinned,"
        " sensitive, use_count, source_app, source_window,"
        " (blob_data IS NOT NULL AND LENGTH(blob_data) > 0) AS has_blob"
        " FROM entries");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY timestamp_ms DESC, id DESC LIMIT :lim");

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
        results.append(recordFromSummary(query));
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
        cursor = PageCursor{true, page.last().timestamp, page.last().id};
    }
    return all;
}

QVector<ClipboardRecord> StorageManager::fetchAllFull(const FilterSpec &filter) const
{
    QVector<ClipboardRecord> summaries = fetchAll(filter);
    QVector<ClipboardRecord> full;
    full.reserve(summaries.size());
    for (const ClipboardRecord &summary : summaries) {
        ClipboardRecord record;
        if (fetchFull(summary.id, &record))
            full.append(record);
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

bool StorageManager::fetchFull(qint64 id, ClipboardRecord *out) const
{
    if (!m_db.isOpen() || !out)
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, timestamp_ms, content_type, content_hash, text_data, blob_data, preview,"
        " size_bytes, pinned, sensitive, use_count, source_app, source_window"
        " FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next()) {
        if (!query.isValid())
            qWarning("egoboard: fetchFull(%lld) failed: %s", id, qPrintable(query.lastError().text()));
        return false;
    }
    out->id = query.value(0).toLongLong();
    out->timestamp = query.value(1).toLongLong();
    out->type = static_cast<ContentType>(query.value(2).toInt());
    out->hash = query.value(3).toByteArray();
    out->textData = query.value(4).toString();
    out->blobData = query.value(5).toByteArray();
    out->hasBlob = !out->blobData.isEmpty();
    out->preview = query.value(6).toString();
    out->sizeBytes = query.value(7).toLongLong();
    out->pinned = query.value(8).toInt() != 0;
    out->sensitive = query.value(9).toInt() != 0;
    out->useCount = query.value(10).toInt();
    out->sourceApp = query.value(11).toString();
    out->sourceWindow = query.value(12).toString();
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
    if (removed > 0)
        emit entriesRemoved({id});
    return removed > 0;
}

int StorageManager::removeEntries(const QList<qint64> &ids)
{
    if (!m_db.isOpen() || ids.isEmpty())
        return 0;
    int removed = 0;
    m_db.transaction();
    for (const qint64 id : ids) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("DELETE FROM entries WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), id);
        if (query.exec())
            removed += query.numRowsAffected();
    }
    m_db.commit();
    if (removed > 0)
        emit entriesRemoved(ids);
    return removed;
}

int StorageManager::clearHistory(bool includePinned)
{
    if (!m_db.isOpen())
        return 0;
    QSqlQuery query(m_db);
    const QString sql = includePinned ? QStringLiteral("DELETE FROM entries")
                                      : QStringLiteral("DELETE FROM entries WHERE pinned = 0");
    if (!query.exec(sql)) {
        qWarning("egoboard: clearHistory failed: %s", qPrintable(query.lastError().text()));
        return 0;
    }
    const int removed = query.numRowsAffected();
    if (removed > 0)
        emit storageReset();
    return removed;
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
    emit pinnedChanged(id, pinned);
    return true;
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
    return stats;
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

bool StorageManager::exec(const QString &sql) const
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        qWarning("egoboard: exec failed: %s (%s)", qPrintable(query.lastError().text()),
                 qPrintable(sql));
        return false;
    }
    return true;
}
