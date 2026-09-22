#include <QtTest>

#include "TrayCycle.h"
#include "TrayMenuModel.h"

// Where a wheel scroll over the tray icon lands inside the recent entries.
// Index 0 is the newest entry — what a fresh copy already put on the clipboard —
// so the first upward scroll moves one step deeper into the history.
class TestTrayCycle : public QObject
{
    Q_OBJECT

private slots:
    void firstScrollStepsBackFromTheTopEntry();
    void walksBackwardsAndStopsAtTheOldest();
    void wheelDownComesForwardAgain();
    void emptyHistoryIsNotAnIndex();
    void shorterHistoryClampsIntoRange();
    void resetStartsOverAtTheTop();
    void modeTransitionsFollowTheVisibilityPolicy();
    void headerNamesPauseStateAndCount();
    void tooltipUpdatesForPauseAndCapture();
    void emptyMenuOffersAClearDisabledState();
    void recentRowsCarryTypeIconsAndBoundedLabels();
    void recentRowTooltipsCarryFullMeta();

private:
    static ClipboardRecord probeRecord(qint64 id, ContentType type, const QString &preview,
                                       const QString &app, qint64 timestamp, qint64 sizeBytes,
                                       bool pinned);
};

void TestTrayCycle::firstScrollStepsBackFromTheTopEntry()
{
    TrayCycle cycle;
    QCOMPARE(cycle.position(), 0); // not cycling yet
    QCOMPARE(cycle.advance(1, 5), 1);
    QCOMPARE(cycle.position(), 2); // "2 of 5" — one older than the newest
    QCOMPARE(cycle.advance(1, 5), 2);
}

void TestTrayCycle::walksBackwardsAndStopsAtTheOldest()
{
    TrayCycle cycle;
    for (int expected = 1; expected <= 4; ++expected)
        QCOMPARE(cycle.advance(1, 5), expected);
    // Clamped, not wrapped: scrolling on does not jump back to the newest.
    QCOMPARE(cycle.advance(1, 5), 4);
    QCOMPARE(cycle.advance(3, 5), 4);
    QCOMPARE(cycle.position(), 5);
}

void TestTrayCycle::wheelDownComesForwardAgain()
{
    TrayCycle cycle;
    QCOMPARE(cycle.advance(1, 6), 1);
    QCOMPARE(cycle.advance(1, 6), 2);
    QCOMPARE(cycle.advance(-1, 6), 1);
    // And at the top it stays on the newest entry.
    QCOMPARE(cycle.advance(-1, 6), 0);
    QCOMPARE(cycle.advance(-4, 6), 0);
    QCOMPARE(cycle.position(), 1);
}

void TestTrayCycle::emptyHistoryIsNotAnIndex()
{
    TrayCycle cycle;
    QCOMPARE(cycle.advance(1, 0), -1);
    QCOMPARE(cycle.position(), 0);
    QCOMPARE(cycle.advance(-1, 0), -1);
}

void TestTrayCycle::shorterHistoryClampsIntoRange()
{
    // Entries may have been deleted (or aged out) since the last scroll.
    TrayCycle cycle;
    QCOMPARE(cycle.advance(1, 9), 1);
    QCOMPARE(cycle.advance(1, 9), 2);
    QCOMPARE(cycle.advance(1, 3), 2); // 3 entries now: index 2 is the last
    QCOMPARE(cycle.advance(1, 3), 2);
}

void TestTrayCycle::resetStartsOverAtTheTop()
{
    TrayCycle cycle;
    QCOMPARE(cycle.advance(1, 4), 1);
    QCOMPARE(cycle.advance(1, 4), 2);
    cycle.reset();
    QCOMPARE(cycle.position(), 0);
    // The next scroll lands one step back from the newest again, not where the
    // previous walk stopped.
    QCOMPARE(cycle.advance(1, 4), 1);
}

ClipboardRecord TestTrayCycle::probeRecord(qint64 id, ContentType type, const QString &preview,
                                            const QString &app, qint64 timestamp, qint64 sizeBytes,
                                            bool pinned)
{
    ClipboardRecord record;
    record.id = id;
    record.type = type;
    record.preview = preview;
    record.sourceApp = app;
    record.timestamp = timestamp;
    record.sizeBytes = sizeBytes;
    record.pinned = pinned;
    return record;
}

void TestTrayCycle::modeTransitionsFollowTheVisibilityPolicy()
{
    // U18: auto follows the history, always stays, hidden removes the surface
    // (hotkeys and the process keep running) — settled without a restart.
    using TrayMenuModel::Mode;
    QCOMPARE(TrayMenuModel::parseMode(QStringLiteral("auto")), Mode::Auto);
    QCOMPARE(TrayMenuModel::parseMode(QStringLiteral("always")), Mode::Always);
    QCOMPARE(TrayMenuModel::parseMode(QStringLiteral("hidden")), Mode::Hidden);
    QCOMPARE(TrayMenuModel::parseMode(QStringLiteral("bogus")), Mode::Auto);
    QCOMPARE(TrayMenuModel::parseMode(QString()), Mode::Auto);

    QVERIFY(!TrayMenuModel::isVisible(Mode::Auto, 0)); // empty history: no icon
    QVERIFY(TrayMenuModel::isVisible(Mode::Auto, 1)); // first capture: icon appears
    QVERIFY(TrayMenuModel::isVisible(Mode::Auto, 50000));
    QVERIFY(TrayMenuModel::isVisible(Mode::Always, 0));
    QVERIFY(TrayMenuModel::isVisible(Mode::Always, 7));
    QVERIFY(!TrayMenuModel::isVisible(Mode::Hidden, 0));
    QVERIFY(!TrayMenuModel::isVisible(Mode::Hidden, 50000));
}

void TestTrayCycle::headerNamesPauseStateAndCount()
{
    // Menu section + tooltip subtitle: state first, then the count.
    QCOMPARE(TrayMenuModel::headerText(false, 12), QStringLiteral("Capturing · 12 entries"));
    QCOMPARE(TrayMenuModel::headerText(true, 12), QStringLiteral("Paused · 12 entries"));
    QCOMPARE(TrayMenuModel::headerText(false, 1), QStringLiteral("Capturing · 1 entry"));
    QCOMPARE(TrayMenuModel::headerText(true, 0), QStringLiteral("Paused · 0 entries"));
}

void TestTrayCycle::tooltipUpdatesForPauseAndCapture()
{
    // Empty history gets an explicit empty state, never a bare tooltip.
    QCOMPARE(TrayMenuModel::tooltipText(false, 0, 0, 1700000000000),
             QStringLiteral("No entries yet — copy something first"));
    QVERIFY(TrayMenuModel::tooltipText(true, 0, 0, 1700000000000)
                .contains(QStringLiteral("paused"), Qt::CaseInsensitive));

    // A fresh capture reads live; an older one names its age.
    const QString fresh = TrayMenuModel::tooltipText(false, 3, 1700000000000 - 5000, 1700000000000);
    QVERIFY(fresh.contains(QStringLiteral("3 entries")));
    QVERIFY(fresh.contains(QStringLiteral("just now")));
    const QString aged = TrayMenuModel::tooltipText(false, 3, 1700000000000 - 5 * 60000, 1700000000000);
    QVERIFY(aged.contains(QStringLiteral("5 min ago")));
    // Pause is visible in the tooltip itself, not just the icon.
    const QString paused =
        TrayMenuModel::tooltipText(true, 3, 1700000000000 - 5000, 1700000000000);
    QVERIFY(paused.contains(QStringLiteral("paused"), Qt::CaseInsensitive));
    QVERIFY(paused.contains(QStringLiteral("3 entries")));
}

void TestTrayCycle::emptyMenuOffersAClearDisabledState()
{
    const auto rows = TrayMenuModel::emptyRows();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().id, qint64(0));
    QVERIFY(!rows.first().enabled); // never pastes
    QVERIFY(!rows.first().label.isEmpty());
}

void TestTrayCycle::recentRowsCarryTypeIconsAndBoundedLabels()
{
    const qint64 now = 1700000000000;
    const QVector<ClipboardRecord> recents = {
        probeRecord(1, ContentType::Text, QStringLiteral("hello world"), QStringLiteral("firefox"),
                    now - 1000, 11, false),
        probeRecord(2, ContentType::RichText, QString(), QStringLiteral("writer"), now - 2000, 40,
                    false),
        probeRecord(3, ContentType::Image, QString(), QStringLiteral("camera"), now - 3000, 2048,
                    true),
        probeRecord(4, ContentType::Files, QStringLiteral("/tmp/a.txt"), QString(), now - 4000, 9,
                    false),
        probeRecord(5, ContentType::Text, QString(200, QLatin1Char('x')), QString(), now - 5000, 200,
                    false),
    };
    const auto rows = TrayMenuModel::buildRecentRows(recents, now);
    QCOMPARE(rows.size(), 5);
    // Type-aware icons, straight from the freedesktop names the UI resolves.
    QCOMPARE(rows.at(0).iconName, QStringLiteral("text-plain"));
    QCOMPARE(rows.at(1).iconName, QStringLiteral("text-html"));
    QCOMPARE(rows.at(2).iconName, QStringLiteral("image-x-generic"));
    QCOMPARE(rows.at(3).iconName, QStringLiteral("folder"));
    // Labels keep the preview plus the source app…
    QVERIFY(rows.at(0).label.contains(QStringLiteral("hello world")));
    QVERIFY(rows.at(0).label.contains(QStringLiteral("firefox")));
    // …with sensible fallbacks when there is no preview…
    QVERIFY(rows.at(1).label.contains(QStringLiteral("(empty)")));
    QVERIFY(rows.at(2).label.contains(QStringLiteral("Image")));
    QVERIFY(rows.at(2).label.contains(QStringLiteral("camera")));
    // …and paragraphs never reach the menu uncut.
    QVERIFY(rows.at(4).label.size() <= TrayMenuModel::kRecentLabelMax + 1);
    QVERIFY(rows.at(4).label.endsWith(QChar(0x2026)));
    for (const auto &row : rows) {
        QVERIFY(row.enabled);
        QVERIFY(row.id != 0);
    }
}

void TestTrayCycle::recentRowTooltipsCarryFullMeta()
{
    const qint64 now = 1700000000000;
    const auto rows = TrayMenuModel::buildRecentRows(
        {probeRecord(9, ContentType::Image, QString(), QStringLiteral("camera"), now - 120000, 2048,
                     true)},
        now);
    QCOMPARE(rows.size(), 1);
    const QString tip = rows.first().toolTip;
    QVERIFY(tip.contains(QStringLiteral("Image")));
    QVERIFY(tip.contains(QStringLiteral("camera")));
    QVERIFY(tip.contains(QStringLiteral("2 min ago")));
    QVERIFY(tip.contains(QStringLiteral("pinned")));
    QVERIFY(!tip.isEmpty());
}

QTEST_MAIN(TestTrayCycle)
#include "tst_traycycle.moc"
