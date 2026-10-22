#include <QtTest>

#include "SearchEngine.h"
#include "SensitiveDataDetector.h"
#include "StorageManager.h"
#include "TransformEngine.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include <functional>

// Speed tests for the hot paths the micro-optimizations touch:
//   - capture write path (prepared-statement cache: find/touch/insert)
//   - page fetches (FTS availability probe cache; per-keystroke search)
//   - bulk soft-delete/restore loops (statements hoisted from the loops)
//   - query parsing / term extraction (tokenize + dedup)
//   - sensitive-data scanning (short-circuit isSensitive)
//   - transform descriptor lookups (built-once descriptor table)
//
// Architecture-speed assertions stay deliberately loose (order-of-magnitude
// tripwires), so an honest machine never fails and only a quadratic-style
// regression trips them. The exact per-op numbers are printed for humans.

class TestPerf : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void reportsMicrosecondTimings();

    // QBENCHMARK slots: run ./tests/tst_perf 'bench*' (or -functions) for the
    // calibrated msec-per-iteration numbers inside a normal run.
    void benchCaptureDedupRepeatCopy();
    void benchCaptureFreshInsert();
    void benchFetchPageRecents();
    void benchFetchPageSearch();
    void benchSoftDeleteAndRestoreRoundTrip();

private:
    void populate(const QString &prefix, int count, qint64 *lastId = nullptr);
    qint64 microsecondsPerCall(int iterations, const std::function<void()> &fn);
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
};

void TestPerf::init()
{
    delete m_storage; // deleteLater() never runs without an event loop
    const QString path = m_dir.filePath(
        QStringLiteral("perf-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
}

namespace {

// Realistic capture payload: sourceApp/window left empty (like headless tests).
ClipboardRecord textRecord(const QByteArray &hash, const QString &text, qint64 timestamp)
{
    ClipboardRecord record;
    record.hash = hash;
    record.type = ContentType::Text;
    record.textData = text;
    record.preview = text.simplified().left(180);
    record.sizeBytes = record.textData.toUtf8().size();
    record.timestamp = timestamp;
    return record;
}

} // namespace

// Populates {count} entries prefixed {prefix}; *lastID receives the final id.
void TestPerf::populate(const QString &prefix, int count, qint64 *lastId)
{
    QVERIFY(m_storage->beginBulk());
    qint64 lastInsertId = 0;
    const qint64 baseTimestampMs = 1'700'000'000'000;
    for (int i = 0; i < count; ++i) {
        const QString text =
            QStringLiteral("%1 entry %2: lorem ipsum dolor sit amet, consectetur adipiscing elite")
                .arg(prefix)
                .arg(i);
        const ClipboardRecord record = textRecord((prefix + QStringLiteral("-h%1")).arg(i).toLatin1(),
                                                  text, baseTimestampMs - i * 1000);
        const qint64 id = m_storage->insertOrUpdate(record);
        QVERIFY(id != 0);
        lastInsertId = id;
    }
    QVERIFY(m_storage->endBulk(true));
    if (lastId)
        *lastId = lastInsertId;
}

qint64 TestPerf::microsecondsPerCall(int iterations, const std::function<void()> &fn)
{
    fn(); // warm-up: force lazy statics (compiled regexes, descriptor table)
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < iterations; ++i)
        fn();
    return timer.nsecsElapsed() / iterations / 1000; // ns -> µs per call
}

// Information-only report: always runs, prints µs/op so rankings show up in
// verbose ctest output; the inline tripwires stay tens-of-times loose.
void TestPerf::reportsMicrosecondTimings()
{
    // --- query parsing / term extraction (search box, per keystroke) -----
    const QString compositeQuery =
        QStringLiteral(
            "lorem ipsum OR dolor \"quoted phrase\" app:firefox type:text -spam pinned:yes "
            "before:30m /reg.*ex/");
    const qint64 parseUs = microsecondsPerCall(
        500, [&] { SearchEngine::parseQuery(compositeQuery); });
    qInfo() << "SearchEngine::parseQuery  (10-token mixed query):" << parseUs << "us/op";
    QVERIFY(parseUs < 2000); // tripwire: pure string work is microseconds

    const qint64 termsUs = microsecondsPerCall(
        500, [&] { SearchEngine::textTerms(compositeQuery); });
    qInfo() << "SearchEngine::textTerms   (dedup via hash set):" << termsUs << "us/op";
    QVERIFY(termsUs < 2000);

    // --- sensitive-data detection (per capture) --------------------------
    QString clean;
    for (int i = 0; i < 40; ++i)
        clean += QStringLiteral("plain clipboard line %1 with no secrets at all.\n").arg(i);
    const qint64 cleanUs = microsecondsPerCall(
        200, [&] { SensitiveDataDetector::isSensitive(clean); });
    qInfo() << "isSensitive (2KB clean text, short-circuit):" << cleanUs << "us/op";
    QVERIFY(cleanUs < 2000);

    const QString dirty = clean + QStringLiteral("password = s3cr3t API key sk-a1b2c3d4e5f6g7h8i9j0 "
                                                 "card 4111 1111 1111 1111 bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9");
    const qint64 dirtyScanUs = microsecondsPerCall(
        200, [&] { SensitiveDataDetector::kinds(dirty); });
    qInfo() << "detector::kinds (text with several secrets):" << dirtyScanUs << "us/op";
    QVERIFY(!SensitiveDataDetector::kinds(dirty).isEmpty());

    // --- transform descriptor lookups (palette/transform plumbing) -------
    const qint64 descriptorUs = microsecondsPerCall(
        2000,
        [&] {
            QVERIFY(TransformEngine::idForName(QStringLiteral("base64-encode")).has_value());
            QVERIFY(TransformEngine::nameForId(TransformEngine::TransformId::SortLines)
                    == QStringLiteral("sort-lines"));
        });
    qInfo() << "TransformEngine descriptor/id lookup:" << descriptorUs << "us/op";
    QVERIFY(descriptorUs < 500);
}

void TestPerf::benchCaptureDedupRepeatCopy()
{
    // The dominant capture path: the same content re-copied after the first
    // time (find + touch + commit). Before the statement cache this prepared
    // two long UPDATE/SELECT texts on every capture.
    const QString text = QStringLiteral("repeat copy payload used by the benchmark");
    const ClipboardRecord first = textRecord("bench-dedup-h0", text, 1'700'000'000'000);
    const qint64 id = m_storage->insertOrUpdate(first);
    QVERIFY(id != 0);

    ClipboardRecord repeat = first;
    repeat.timestamp += 3000; // the "same content copied again later" case
    QBENCHMARK { QCOMPARE(m_storage->insertOrUpdate(repeat), id); }
    QCOMPARE(m_storage->stats().entryCount, qint64(1)); // still exactly one row
}

void TestPerf::benchCaptureFreshInsert()
{
    // Fresh content per iteration: hash-different, so the insert arm of
    // insertOrUpdate (find miss + INSERT + commit) is measured.
    int counter = 0;
    QBENCHMARK
    {
        const ClipboardRecord record =
            textRecord(QStringLiteral("bench-fresh-%1").arg(counter).toLatin1(),
                       QStringLiteral("fresh capture %1").arg(counter),
                       1'700'000'000'000 + counter);
        ++counter;
        QVERIFY(m_storage->insertOrUpdate(record) != 0);
    }
}

void TestPerf::benchFetchPageRecents()
{
    // The unfiltered Newest page: what the list shows on every refresh.
    populate(QStringLiteral("perfrec"), 600);
    const FilterSpec trivialFilter;
    bool hasMore = false;
    QBENCHMARK
    {
        const auto page = m_storage->fetchPage(trivialFilter, {}, 50, &hasMore);
        QCOMPARE(page.size(), 50);
        QVERIFY(hasMore);
    }
}

void TestPerf::benchFetchPageSearch()
{
    // A text search page: FTS availability is now probed once (was two SQL
    // statements per keystroke) plus the MATCH on the whole prefix set.
    populate(QStringLiteral("perfsearch"), 600);
    FilterSpec filter;
    filter.searchText = QStringLiteral("lorem dolor");
    QBENCHMARK
    {
        bool hasMore = false;
        const auto page = m_storage->fetchPage(filter, {}, 50, &hasMore);
        QCOMPARE(page.size(), 50);
    }
}

void TestPerf::benchSoftDeleteAndRestoreRoundTrip()
{
    // The two per-row statement loops measure together: soft-delete N entries,
    // then restore them exactly, in one transaction each (N = 50 per iteration).
    populate(QStringLiteral("perftrash"), 50);
    QList<qint64> ids;
    bool hasMore = false;
    const auto page = m_storage->fetchPage(FilterSpec{}, {}, 50, &hasMore);
    QCOMPARE(page.size(), 50);
    for (const ClipboardRecord &row : page)
        ids.append(row.id);

    QBENCHMARK
    {
        QCOMPARE(m_storage->softDeleteEntries(ids).size(), 50);
        QCOMPARE(m_storage->trashCount(), 50);
        QCOMPARE(m_storage->restoreTrashEntries(ids), 50);
        QCOMPARE(m_storage->trashCount(), 0);
    }
    QCOMPARE(m_storage->stats().entryCount, qint64(50)); // round trip lost nothing
}

QTEST_GUILESS_MAIN(TestPerf)
#include "tst_perf.moc"
