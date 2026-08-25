#pragma once

#include "ClipboardRecord.h"
#include "FilterSpec.h"

#include <QList>
#include <QObject>
#include <QVector>

struct StorageStats {
    qint64 entryCount = 0;
    qint64 totalBytes = 0;
    qint64 pinnedCount = 0;
    qint64 imageCount = 0;
    qint64 ocrCount = 0; // images with recognized text
    qint64 sensitiveCount = 0; // entries flagged sensitive (Mark/Redact modes)
};

// Keyset cursor for infinite scroll: identifies the last row of the previous
// page (ORDER BY timestamp_ms DESC, id DESC).
struct PageCursor {
    bool valid = false;
    qint64 timestampMs = 0;
    qint64 id = 0;
};

// Storage seam: everything that reads/writes history goes through this
// interface so unit tests can substitute an in-memory implementation.
class IClipboardStorage : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Inserts the record, or - when the content hash already exists - bumps
    // the existing row's timestamp/use count instead (deduplication).
    // Returns the row id; sets *updatedExisting when an old row was touched.
    virtual qint64 insertOrUpdate(const ClipboardRecord &record, bool *updatedExisting = nullptr) = 0;

    virtual QVector<ClipboardRecord> fetchPage(const FilterSpec &filter, const PageCursor &cursor,
                                               int limit, bool *hasMore = nullptr) const = 0;

    virtual bool fetchFull(qint64 id, ClipboardRecord *out) const = 0;

    virtual bool remove(qint64 id) = 0;
    virtual int removeEntries(const QList<qint64> &ids) = 0;
    // Deletes all entries (or all non-pinned when includePinned == false).
    virtual int clearHistory(bool includePinned) = 0;

    virtual bool setPinned(qint64 id, bool pinned) = 0;
    virtual bool setOcrText(qint64 id, const QString &ocrText) = 0;

    virtual QStringList sourceApps() const = 0;
    virtual StorageStats stats() const = 0;
    // Deletes oldest non-pinned entries until total size <= maxBytes.
    virtual int enforceDiskCap(qint64 maxBytes) = 0;

signals:
    void entryAdded(qint64 id);
    void entryTouched(qint64 id); // dedup hit: moved to top of history
    void entriesRemoved(const QList<qint64> &ids);
    void storageReset();
    void pinnedChanged(qint64 id, bool pinned);
};
