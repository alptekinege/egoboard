#pragma once

#include "ClipboardRecord.h"
#include "ContentType.h"

#include <QMap>
#include <QString>
#include <QVector>

class IClipboardStorage;

// P2-A usage dashboard: read-only local aggregates over the existing history
// indexes. Payload text is never read for display — only counts, sums and
// group keys (day, app, type, size bucket) leave the database. No schema
// change; collection runs on the caller's (GUI) thread in bounded queries and
// never VACUUMs.
struct DashboardDay {
    qint64 dayStartMs = 0; // local-midnight start of the day
    int count = 0;
};

struct DashboardTopApp {
    QString app;
    int count = 0;
};

struct DashboardStats {
    qint64 totalEntries = 0;
    qint64 totalBytes = 0;
    qint64 pinnedCount = 0;
    qint64 sensitiveCount = 0;
    qint64 imageCount = 0;
    qint64 ocrCount = 0;
    // ContentType value (int) -> entry count. Missing keys count as zero.
    QMap<int, int> byType;
    // Top source apps by entry count, descending, at most kMaxTopApps.
    QVector<DashboardTopApp> topApps;
    // Last 14 days, oldest first (index 13 == today).
    QVector<DashboardDay> last14Days;
    // Size histogram over size_bytes: [<1KB, 1-10KB, 10-100KB, >=100KB].
    QVector<int> sizeBuckets = QVector<int>(4, 0);
    // Consecutive non-empty days ending at (or just before) today.
    int streakDays = 0;

    bool isEmpty() const { return totalEntries == 0; }
    int countForType(ContentType type) const { return byType.value(static_cast<int>(type), 0); }

    // Day starts (local midnight) for the last 14 days, oldest first.
    static QVector<qint64> last14DayStarts();
    // Streak over day counts ordered oldest-first (last element == today):
    // the trailing run of non-empty days, starting from today or — when today
    // is still empty — from yesterday, so a quiet morning does not reset it.
    static int streakFromDayCounts(const QVector<int> &countsOldestFirst);

    static DashboardStats collect(IClipboardStorage *storage);
};

// Size-bucket index for one entry (0..3, same order as DashboardStats::sizeBuckets).
int dashboardSizeBucket(qint64 sizeBytes);
