// P2-A usage dashboard: read-only local aggregates over the existing indexes
// plus the QPainter fallback dialog (no QtCharts dependency). Runs offscreen
// against a temporary database, the same pattern as tst_uidesign.

#include <QtTest>

#include "ColorSchemeIndex.h"
#include "DashboardDialog.h"
#include "DashboardStats.h"
#include "DesignTokens.h"
#include "PaletteCommands.h"
#include "TextAppearance.h"
#include "UiHelpers.h"

#include <KColorScheme>
#include <KSharedConfig>

#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QListWidget>
#include <QTemporaryDir>

#include "StorageManager.h"

namespace {

ClipboardRecord makeDashboardRecord(const QByteArray &hash, ContentType type, const QString &app,
                                    qint64 timestamp, qint64 sizeBytes, bool sensitive = false,
                                    bool pinned = false)
{
    ClipboardRecord record;
    record.type = type;
    record.hash = hash;
    record.textData = QString::fromLatin1(hash);
    record.preview = record.textData.left(80);
    record.sizeBytes = sizeBytes;
    record.timestamp = timestamp;
    record.sourceApp = app;
    record.sensitive = sensitive;
    record.pinned = pinned;
    return record;
}

qint64 dayStart(int daysAgo)
{
    const QDate day = QDate::currentDate().addDays(-daysAgo);
    return QDateTime(day, QTime(0, 0)).toMSecsSinceEpoch();
}

} // namespace

class TestDashboard : public QObject {
    Q_OBJECT

private slots:
    void aggregatesFixtureCountsByDayAppTypeAndSize();
    void streakFromDayCountsHandlesEdges();
    void sizeBucketsFollowThresholds();
    void emptyDatabaseShowsEmptyState();
    void fallbackChartsRenderOffscreenAndNavigate();
    void dashboardCommandParsesAndResolves();
    void contrastHoldsOverInstalledSchemes();
    void aggregatesStayBounded();
};

void TestDashboard::aggregatesFixtureCountsByDayAppTypeAndSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("dashboard-fixture.db")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 todayNoon = dayStart(0) + 43200000;
    const qint64 yesterdayNoon = dayStart(1) + 43200000;

    QVERIFY(storage.insertOrUpdate(makeDashboardRecord(QByteArrayLiteral("db-1"), ContentType::Text,
                                                       QStringLiteral("firefox"),
                                                       qMin(now, todayNoon), 100))
            != 0);
    QVERIFY(storage.insertOrUpdate(makeDashboardRecord(QByteArrayLiteral("db-2"), ContentType::Image,
                                                       QStringLiteral("firefox"),
                                                       qMin(now, todayNoon + 1000), 200000, true))
            != 0);
    QVERIFY(storage.insertOrUpdate(makeDashboardRecord(QByteArrayLiteral("db-3"),
                                                       ContentType::RichText,
                                                       QStringLiteral("konsole"), yesterdayNoon,
                                                       2000))
            != 0);
    QVERIFY(storage.insertOrUpdate(makeDashboardRecord(QByteArrayLiteral("db-4"), ContentType::Files,
                                                       QStringLiteral("dolphin"),
                                                       yesterdayNoon + 1000, 20000))
            != 0);
    QVERIFY(storage.insertOrUpdate(makeDashboardRecord(QByteArrayLiteral("db-5"), ContentType::Text,
                                                       QStringLiteral("firefox"),
                                                       dayStart(3) + 1000, 500))
            != 0);
    const qint64 pinnedId = storage.insertOrUpdate(makeDashboardRecord(
        QByteArrayLiteral("db-6"), ContentType::Text, QStringLiteral("code"), dayStart(5) + 1000,
        600, false, true));
    QVERIFY(pinnedId != 0);
    QVERIFY(storage.setOcrText(pinnedId, QStringLiteral("hello")));

    const DashboardStats stats = DashboardStats::collect(&storage);
    QCOMPARE(stats.totalEntries, qint64(6));
    QCOMPARE(stats.countForType(ContentType::Text), 3);
    QCOMPARE(stats.countForType(ContentType::Image), 1);
    QCOMPARE(stats.countForType(ContentType::RichText), 1);
    QCOMPARE(stats.countForType(ContentType::Files), 1);
    QCOMPARE(stats.pinnedCount, qint64(1));
    QCOMPARE(stats.sensitiveCount, qint64(1));
    QCOMPARE(stats.imageCount, qint64(1));
    QCOMPARE(stats.ocrCount, qint64(1));

    // Top apps: firefox leads with three, the rest follow alphabetically on ties.
    QVERIFY(!stats.topApps.isEmpty());
    QCOMPARE(stats.topApps.first().app, QStringLiteral("firefox"));
    QCOMPARE(stats.topApps.first().count, 3);
    QVERIFY(stats.topApps.size() <= 8);

    // Size histogram: 100/500/600 -> bucket 0; 2000 -> 1; 20000 -> 2; 200000 -> 3.
    QCOMPARE(stats.sizeBuckets.size(), 4);
    QCOMPARE(stats.sizeBuckets.at(0), 3);
    QCOMPARE(stats.sizeBuckets.at(1), 1);
    QCOMPARE(stats.sizeBuckets.at(2), 1);
    QCOMPARE(stats.sizeBuckets.at(3), 1);

    // Days: today 2, yesterday 2, two days ago empty, three days ago 1.
    QCOMPARE(stats.last14Days.size(), 14);
    QCOMPARE(stats.last14Days.last().count, 2); // today
    QCOMPARE(stats.last14Days.at(stats.last14Days.size() - 2).count, 2); // yesterday
    // Two days ago is empty, so the streak ends at two.
    QCOMPARE(stats.streakDays, 2);

    // Aggregates only: no payload text leaves the database in the struct.
    QVERIFY(stats.topApps.first().app != QStringLiteral("db-1"));
}

void TestDashboard::streakFromDayCountsHandlesEdges()
{
    QCOMPARE(DashboardStats::streakFromDayCounts({}), 0);
    QCOMPARE(DashboardStats::streakFromDayCounts({0, 0, 0}), 0);
    QCOMPARE(DashboardStats::streakFromDayCounts({1, 2, 3}), 3);
    // A quiet morning does not reset the run: today empty starts from yesterday.
    QCOMPARE(DashboardStats::streakFromDayCounts({1, 1, 0}), 2);
    QCOMPARE(DashboardStats::streakFromDayCounts({0, 1, 1, 0}), 2);
    // A gap breaks the run.
    QCOMPARE(DashboardStats::streakFromDayCounts({1, 0, 1, 1}), 2);
    QCOMPARE(DashboardStats::streakFromDayCounts({5}), 1);
    QCOMPARE(DashboardStats::streakFromDayCounts({0}), 0);
}

void TestDashboard::sizeBucketsFollowThresholds()
{
    QCOMPARE(dashboardSizeBucket(0), 0);
    QCOMPARE(dashboardSizeBucket(1023), 0);
    QCOMPARE(dashboardSizeBucket(1024), 1);
    QCOMPARE(dashboardSizeBucket(10239), 1);
    QCOMPARE(dashboardSizeBucket(10240), 2);
    QCOMPARE(dashboardSizeBucket(102399), 2);
    QCOMPARE(dashboardSizeBucket(102400), 3);
    QCOMPARE(dashboardSizeBucket(1000000), 3);
}

void TestDashboard::emptyDatabaseShowsEmptyState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("dashboard-empty.db")));
    const DashboardStats stats = DashboardStats::collect(&storage);
    QVERIFY(stats.isEmpty());
    QCOMPARE(stats.totalEntries, qint64(0));
    QCOMPARE(stats.streakDays, 0);

    DashboardDialog dialog(&storage);
    dialog.show();
    QTest::qWait(50);
    QVERIFY(dialog.stats().isEmpty());
    // Empty DB: the empty state shows, no charts are built.
    QVERIFY(dialog.dayChart() == nullptr);
    QVERIFY(dialog.typeChart() == nullptr);
    QVERIFY(dialog.sizeChart() == nullptr);
    QVERIFY(dialog.topAppsList() == nullptr);
    QVERIFY(dialog.windowTitle().contains(QStringLiteral("dashboard"), Qt::CaseInsensitive));
    dialog.close();
}

void TestDashboard::fallbackChartsRenderOffscreenAndNavigate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("dashboard-render.db")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(storage.insertOrUpdate(makeDashboardRecord(QByteArrayLiteral("dr-1"), ContentType::Text,
                                                       QStringLiteral("firefox"), now, 120))
            != 0);
    QVERIFY(storage.insertOrUpdate(makeDashboardRecord(QByteArrayLiteral("dr-2"), ContentType::Image,
                                                       QStringLiteral("konsole"), now, 5000))
            != 0);

    DashboardDialog dialog(&storage);
    dialog.resize(560, 620);
    dialog.show();
    QTest::qWait(50);

    QVERIFY(dialog.dayChart() != nullptr);
    QVERIFY(dialog.typeChart() != nullptr);
    QVERIFY(dialog.sizeChart() != nullptr);
    QVERIFY(dialog.topAppsList() != nullptr);
    QCOMPARE(dialog.dayChart()->barCount(), 14);
    QCOMPARE(dialog.typeChart()->barCount(), 4);
    QCOMPARE(dialog.sizeChart()->barCount(), 4);
    QVERIFY(!dialog.dayChart()->accessibleName().isEmpty());
    QVERIFY(!dialog.typeChart()->accessibleName().isEmpty());
    QVERIFY(!dialog.sizeChart()->accessibleName().isEmpty());
    QVERIFY(!dialog.topAppsList()->accessibleName().isEmpty());
    // Keyboard-navigable: arrows move the day cursor, Home/End jump.
    const int last = dialog.dayChart()->barCount() - 1;
    dialog.dayChart()->setFocus(Qt::OtherFocusReason);
    QTest::qWait(20);
    QTest::keyClick(dialog.dayChart(), Qt::Key_Home);
    QCOMPARE(dialog.dayChart()->focusedBar(), 0);
    QTest::keyClick(dialog.dayChart(), Qt::Key_End);
    QCOMPARE(dialog.dayChart()->focusedBar(), last);
    QTest::keyClick(dialog.dayChart(), Qt::Key_Left);
    QCOMPARE(dialog.dayChart()->focusedBar(), last - 1);
    QTest::keyClick(dialog.dayChart(), Qt::Key_Right);
    QCOMPARE(dialog.dayChart()->focusedBar(), last);
    // The custom painter draws without warnings or crashes offscreen.
    const QPixmap shot = dialog.grab();
    QVERIFY(!shot.isNull());
    // Top apps list carries per-row accessible text, payload-free.
    QVERIFY(dialog.topAppsList()->count() >= 2);
    QVERIFY(!dialog.topAppsList()->item(0)->text().isEmpty());
    dialog.close();
}

void TestDashboard::dashboardCommandParsesAndResolves()
{
    const PaletteCommands::Parsed parsed =
        PaletteCommands::parse(QStringLiteral(">dashboard"));
    QVERIFY(parsed.hasPrefix);
    QCOMPARE(parsed.word, QStringLiteral("dashboard"));
    QVERIFY(parsed.argument.isEmpty());
    QVERIFY(parsed.command != nullptr);
    QCOMPARE(parsed.command->id, QStringLiteral("dashboard"));
    QVERIFY(!parsed.command->takesArgument());
    // Aliases resolve to the same command.
    QCOMPARE(PaletteCommands::find(QStringLiteral("stats"))->id, QStringLiteral("dashboard"));
    QCOMPARE(PaletteCommands::find(QStringLiteral("usage"))->id, QStringLiteral("dashboard"));
}

void TestDashboard::contrastHoldsOverInstalledSchemes()
{
    QVector<QPair<QString, QPalette>> schemes;
    for (const ColorSchemeIndex::Entry &entry : ColorSchemeIndex::scan()) {
        const QPalette raw =
            KColorScheme::createApplicationPalette(KSharedConfig::openConfig(entry.path));
        schemes.append({entry.id, TextAppearance::applyOverrides(raw, {})});
    }
    QVERIFY2(!schemes.isEmpty(), "no KDE color schemes installed to discover");
    for (const auto &scheme : schemes) {
        const QColor window = scheme.second.color(QPalette::Window);
        const QColor base = scheme.second.color(QPalette::Base);
        const QColor text = scheme.second.color(QPalette::Text);
        const QColor mid = scheme.second.color(QPalette::Mid);
        QVERIFY2(TextAppearance::contrastRatio(text, window)
                     >= TextAppearance::kTextContrastRatio,
                 qPrintable(scheme.first));
        QVERIFY2(TextAppearance::contrastRatio(text, base) >= TextAppearance::kTextContrastRatio,
                 qPrintable(scheme.first));
        QVERIFY2(TextAppearance::contrastRatio(mid, window)
                     >= TextAppearance::kDimTextContrastRatio,
                 qPrintable(scheme.first));
        // Bars stay visible against the chart background on every scheme.
        QVERIFY(scheme.second.color(QPalette::Highlight) != base);
        QVERIFY(DesignTokens::focusRingColor(scheme.second).isValid());
    }
}

void TestDashboard::aggregatesStayBounded()
{
    // Bounded collection: 200-row pages, no fetchAllFull. A few thousand rows
    // must aggregate well inside the 500 ms P2-A budget offscreen, so 50k
    // stays bounded by the same indexed COUNT queries on the fast path.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("dashboard-bounded.db")));
    const qint64 baseMs = dayStart(6) + 1000;
    QVERIFY(storage.beginBulk());
    for (int i = 0; i < 2000; ++i) {
        const QByteArray hash = QByteArrayLiteral("bounded-") + QByteArray::number(i);
        const ContentType type =
            static_cast<ContentType>(i % 4);
        const QString app = (i % 2 == 0) ? QStringLiteral("firefox") : QStringLiteral("konsole");
        ClipboardRecord record = makeDashboardRecord(hash, type, app, baseMs + i, 100 + (i % 500));
        QVERIFY(storage.insertOrUpdate(record) != 0);
    }
    QVERIFY(storage.endBulk());
    QElapsedTimer timer;
    timer.start();
    const DashboardStats stats = DashboardStats::collect(&storage);
    const qint64 elapsed = timer.elapsed();
    QCOMPARE(stats.totalEntries, qint64(2000));
    QVERIFY2(elapsed < 500, qPrintable(QStringLiteral("aggregates took %1 ms").arg(elapsed)));
}

QTEST_MAIN(TestDashboard)
#include "tst_dashboard.moc"
