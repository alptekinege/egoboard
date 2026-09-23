#pragma once

#include "IClipboardStorage.h"

#include <QSqlDatabase>

// SQLite-backed clipboard history. All access happens on the thread that
// constructed the object (GUI thread); VacuumWorker owns its own connection.
class StorageManager : public IClipboardStorage {
    Q_OBJECT
public:
    explicit StorageManager(const QString &databasePath, QObject *parent = nullptr);
    ~StorageManager() override;

    QString databasePath() const { return m_path; }
    qint64 databaseFileSize() const;
    // Shared connection; only use from the thread that owns this object.
    QSqlDatabase database() const { return m_db; }

    // Walks the whole history (paged internally) applying the filter.
    QVector<ClipboardRecord> fetchAll(const FilterSpec &filter) const;
    // Same, but with full payloads (needed for export).
    QVector<ClipboardRecord> fetchAllFull(const FilterSpec &filter) const;

    qint64 insertOrUpdate(const ClipboardRecord &record, bool *updatedExisting = nullptr) override;
    QVector<ClipboardRecord> fetchPage(const FilterSpec &filter, const PageCursor &cursor,
                                       int limit, bool *hasMore = nullptr) const override;
    bool fetchFull(qint64 id, ClipboardRecord *out) const override;
    bool fetchSummary(qint64 id, ClipboardRecord *out) const override;
    bool remove(qint64 id) override;
    int removeEntries(const QList<qint64> &ids) override;
    int clearHistory(bool includePinned) override;
    bool setPinned(qint64 id, bool pinned) override;
    bool setOcrText(qint64 id, const QString &ocrText) override;
    bool touchEntry(qint64 id) override;

    QStringList allTags() const override;
    QStringList tagsForEntry(qint64 entryId) const override;
    bool addTag(qint64 entryId, const QString &tag) override;
    bool removeTag(qint64 entryId, const QString &tag) override;

    QList<SavedSearch> savedSearches() const override;
    qint64 addSavedSearch(const QString &name, const FilterSpec &filter) override;
    bool removeSavedSearch(qint64 id) override;
    QStringList sourceApps() const override;
    StorageStats stats() const override;
    int enforceDiskCap(qint64 maxBytes) override;
    int enforceMaxEntries(qint64 maxEntries) override;

    // Rule-based expiry: deletes entries older than olderThanMs, optionally
    // restricted to a content type and a source-app wildcard ("firefox*").
    // keepPinned=true protects pinned entries from deletion.
    // Returns the number of deleted rows.
    int expireEntries(qint64 olderThanMs, int contentType,
                      const QString &sourceAppWildcard, bool keepPinned = true);
    // U11 soft-delete buffer (trash tables, additive schema): moves rows (+
    // their tag/group links) out of history while preserving ids, timestamps,
    // use-counts and pins, so Undo restores exactly instead of re-inserting
    // as new rows. Trash is invisible to history queries, stats, dedup and
    // caps; rows older than the retention bound are purged on the next
    // soft-delete and at open. Emits entriesRemoved(ids) like removeEntries.
    // Returns the trashed ids (unknown ids are skipped).
    QList<qint64> softDeleteEntries(const QList<qint64> &ids);
    // Soft clear-history variant behind MainWindow::clearHistory's Undo toast.
    QList<qint64> softClearHistory(bool includePinned);
    // Expiry variant behind ExpireScheduler (hard expireEntries stays for its
    // tests and callers that want immediate deletion).
    QList<qint64> expireEntriesToTrash(qint64 olderThanMs, int contentType,
                                       const QString &sourceAppWildcard, bool keepPinned = true);
    // Restores exactly the given trash ids. Rows whose hash went live again
    // are dropped instead of duplicated (their content is already present).
    // Emits storageReset() when anything was restored. Returns restored count.
    int restoreTrashEntries(const QList<qint64> &ids);
    // Hard-deletes trash rows with trashed_ms <= olderThanMs. Returns count.
    int purgeTrash(qint64 olderThanMs);
    int trashCount() const;

    // Opens an SQLCipher database: the key must be applied before the first
    // statement, so the constructor skips schema setup when the file is
    // encrypted and this finishes it. False when SQLCipher is unavailable or
    // the key is rejected.
    bool setEncryptionKey(const QString &key);
    // Encrypts the open plaintext database in place (non-empty key) or decrypts
    // it (empty key). Refuses to enable encryption when SQLCipher is not built.
    bool changeEncryptionKey(const QString &newKey);
    bool verifyEncryptionKey() const;
    bool isSqlCipherAvailable() const;
    QString cipherVersion() const;
    bool isEncrypted() const { return m_encrypted; }
    // The database file exists but is not a plain SQLite database (its header
    // is SQLCipher's random salt), i.e. it can only be opened with a key.
    bool requiresEncryptionKey() const;

    // PRAGMA quick_check: false when the file is damaged; *error carries the
    // first reported problem. Runs on the caller's (GUI) connection.
    bool quickCheck(QString *error = nullptr) const;
    // Rebuilds the FTS index; repairs search without touching the history.
    bool rebuildSearchIndex();

    // Announces an out-of-band change (e.g. a backup restored on a worker
    // thread) so models and views reload from page one.
    void notifyStorageReset() { emit storageReset(); }

    // Batched writes (imports, bulk edits): one outer transaction plus
    // suppression of the per-row signals, so an import is a single commit and
    // a single refresh instead of thousands of signals. Always pair with
    // endBulk(). Nested calls are counted.
    bool beginBulk();
    bool endBulk(bool commit = true);
    bool isBulkActive() const { return m_bulkDepth > 0; }

private:
    static ClipboardRecord recordFromSummary(const QSqlQuery &query);
    // Row parsers for the two column sets (list summary vs. full payload).
    static ClipboardRecord recordFromFull(const QSqlQuery &query);

    // One SQL page fetch; includeText adds text_data/ocr_text to the row (used
    // by the regex scan, which needs the payloads to match against).
    QVector<ClipboardRecord> fetchPageSql(const FilterSpec &filter, const PageCursor &cursor,
                                          int limit, bool *hasMore, bool includeText) const;
    // Regex pages scan the (capped) history, verifying rows in C++.
    QVector<ClipboardRecord> fetchPageRegex(const FilterSpec &filter, const PageCursor &cursor,
                                            int limit, bool *hasMore) const;

    // Reentrant transaction helpers: SQLite does not nest, so inner scopes
    // join the outermost transaction (used by the importer's bulk mode).
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    bool signalsSuppressed() const { return m_bulkDepth > 0; }

    QString m_path;
    QString m_connectionName; // unique per instance (tests create several)
    QSqlDatabase m_db;
    bool m_encrypted = false;
    int m_transactionDepth = 0;
    int m_bulkDepth = 0;
};
