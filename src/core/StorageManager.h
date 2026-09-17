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
