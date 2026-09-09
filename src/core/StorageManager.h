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

    bool setEncryptionKey(const QString &key);
    bool changeEncryptionKey(const QString &newKey);
    bool verifyEncryptionKey() const;
    bool isSqlCipherAvailable() const;
    QString cipherVersion() const;
    bool isEncrypted() const { return m_encrypted; }

private:
    static ClipboardRecord recordFromSummary(const QSqlQuery &query);
    bool exec(const QString &sql) const;

    QString m_path;
    QString m_connectionName; // unique per instance (tests create several)
    QSqlDatabase m_db;
    std::atomic_int m_insertCounter{0}; // throttles enforceDiskCap frequency
    bool m_encrypted = false;
};
