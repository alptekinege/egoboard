#include "DashboardStats.h"

#include "FilterSpec.h"
#include "IClipboardStorage.h"
#include "StorageManager.h"

#include <QDate>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>

#include <algorithm>

namespace {

constexpr int kDashboardPage = 200;
constexpr int kDashboardTopApps = 8;
constexpr qint64 kDayMs = 86400000;

} // namespace

QVector<qint64> DashboardStats::last14DayStarts()
{
    QVector<qint64> starts;
    starts.reserve(14);
    const QDate today = QDate::currentDate();
    for (int back = 13; back >= 0; --back) {
        const QDate day = today.addDays(-back);
        starts.append(QDateTime(day, QTime(0, 0)).toMSecsSinceEpoch());
    }
    return starts;
}

int DashboardStats::streakFromDayCounts(const QVector<int> &countsOldestFirst)
{
    if (countsOldestFirst.isEmpty())
        return 0;
    int index = countsOldestFirst.size() - 1;
    // A quiet morning is not a broken streak: when today is still empty the
    // run starts from yesterday.
    if (countsOldestFirst.at(index) == 0)
        --index;
    int streak = 0;
    while (index >= 0 && countsOldestFirst.at(index) > 0) {
        ++streak;
        --index;
    }
    return streak;
}

int dashboardSizeBucket(qint64 sizeBytes)
{
    if (sizeBytes < 1024)
        return 0;
    if (sizeBytes < 10 * 1024)
        return 1;
    if (sizeBytes < 100 * 1024)
        return 2;
    return 3;
}

DashboardStats DashboardStats::collect(IClipboardStorage *storage)
{
    DashboardStats stats;
    if (!storage)
        return stats;

    const StorageStats base = storage->stats();
    stats.totalEntries = base.entryCount;
    stats.totalBytes = base.totalBytes;
    stats.pinnedCount = base.pinnedCount;
    stats.sensitiveCount = base.sensitiveCount;
    stats.imageCount = base.imageCount;
    stats.ocrCount = base.ocrCount;

    const QVector<qint64> dayStarts = last14DayStarts();
    stats.last14Days.reserve(dayStarts.size());
    for (qint64 start : dayStarts)
        stats.last14Days.append({start, 0});

    // Fast path: direct aggregate queries over the existing indexes (GUI
    // thread, same connection the storage owns). No payload column is
    // selected — only counts and group keys.
    if (auto *manager = qobject_cast<StorageManager *>(storage)) {
        const QSqlDatabase db = manager->database();
        if (db.isOpen()) {
            {
                QSqlQuery query(db);
                if (query.exec(QStringLiteral(
                        "SELECT content_type, COUNT(*) FROM entries GROUP BY content_type"))) {
                    while (query.next())
                        stats.byType.insert(query.value(0).toInt(), query.value(1).toInt());
                }
            }
            {
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                    "SELECT source_app, COUNT(*) AS n FROM entries"
                    " WHERE source_app IS NOT NULL AND source_app <> ''"
                    " GROUP BY source_app ORDER BY n DESC, source_app COLLATE NOCASE LIMIT :lim"));
                query.bindValue(QStringLiteral(":lim"), kDashboardTopApps);
                if (query.exec()) {
                    while (query.next())
                        stats.topApps.append({query.value(0).toString(), query.value(1).toInt()});
                }
            }
            {
                QSqlQuery query(db);
                if (query.exec(QStringLiteral(
                        "SELECT"
                        " COUNT(CASE WHEN size_bytes < 1024 THEN 1 END),"
                        " COUNT(CASE WHEN size_bytes >= 1024 AND size_bytes < 10240 THEN 1 END),"
                        " COUNT(CASE WHEN size_bytes >= 10240 AND size_bytes < 102400 THEN 1 END),"
                        " COUNT(CASE WHEN size_bytes >= 102400 THEN 1 END)"
                        " FROM entries"))) {
                    if (query.next()) {
                        for (int i = 0; i < 4; ++i)
                            stats.sizeBuckets[i] = query.value(i).toInt();
                    }
                }
            }
            for (int i = 0; i < dayStarts.size(); ++i) {
                const qint64 from = dayStarts.at(i);
                const qint64 to = from + kDayMs - 1;
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                    "SELECT COUNT(*) FROM entries WHERE timestamp_ms >= :from AND timestamp_ms <= :to"));
                query.bindValue(QStringLiteral(":from"), from);
                query.bindValue(QStringLiteral(":to"), to);
                if (query.exec() && query.next())
                    stats.last14Days[i].count = query.value(0).toInt();
            }
        }
    } else {
        // Generic path (fakes, future stores): bounded 200-row summary pages,
        // never fetchAllFull. Totals above (stats()) stay authoritative.
        QMap<QString, int> appCounts;
        FilterSpec filter; // trivial: whole history, Newest order
        PageCursor cursor;
        while (true) {
            bool hasMore = false;
            const QVector<ClipboardRecord> page = storage->fetchPage(filter, cursor, kDashboardPage,
                                                                    &hasMore);
            if (page.isEmpty())
                break;
            for (const ClipboardRecord &record : page) {
                stats.byType[static_cast<int>(record.type)] += 1;
                if (!record.sourceApp.isEmpty())
                    appCounts[record.sourceApp] += 1;
                stats.sizeBuckets[dashboardSizeBucket(record.sizeBytes)] += 1;
                for (int i = 0; i < dayStarts.size(); ++i) {
                    if (record.timestamp >= dayStarts.at(i)
                        && record.timestamp < dayStarts.at(i) + kDayMs) {
                        stats.last14Days[i].count += 1;
                        break;
                    }
                }
            }
            if (!hasMore)
                break;
            const ClipboardRecord &last = page.constLast();
            cursor = PageCursor{true, last.timestamp, last.id, last.useCount};
        }
        QList<QPair<QString, int>> sorted;
        for (auto it = appCounts.cbegin(); it != appCounts.cend(); ++it)
            sorted.append({it.key(), it.value()});
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            if (a.second != b.second)
                return a.second > b.second;
            return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
        });
        for (int i = 0; i < qMin(kDashboardTopApps, sorted.size()); ++i)
            stats.topApps.append({sorted.at(i).first, sorted.at(i).second});
    }

    QVector<int> dayCounts;
    dayCounts.reserve(stats.last14Days.size());
    for (const DashboardDay &day : stats.last14Days)
        dayCounts.append(day.count);
    stats.streakDays = streakFromDayCounts(dayCounts);
    return stats;
}
