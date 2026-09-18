#include <QtTest>

#include "TrayCycle.h"

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

QTEST_MAIN(TestTrayCycle)
#include "tst_traycycle.moc"
