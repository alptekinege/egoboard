#include <QtTest>

#include "ClipboardListModel.h"
#include "StorageManager.h"

#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestStorage : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void insertAndFetch();
    void dedupUpdatesTimestamp();
    void pagination();
    void filters();
    void pinnedAndRemove();
    void diskCap();
    void maxEntriesCap();
    void touchEntryBump();
    void emitsHistorySignals();
    void modelRefreshesOnHistoryChanges();
    void binaryPayloadAndMetadata();
    void searchEscapesLikeCharacters();
    void diskCapPreservesPinnedEntries();
    void sensitiveFilterAndStats();
    void expireRules();
    void bulkRemovalAndSignal();
    void clearHistorySelectiveAndTotal();
    void ocrTextStorageAndStats();
    void sourceAppsDistinctListing();
    void rejectsInvalidRequests();
    void appliesInclusiveTimeBoundsAndCombinedFilters();
    void paginatesEntriesWithEqualTimestamps();
    void clearsOcrTextAndReturnsFullPayloads();

private:
    ClipboardRecord makeRecord(const QByteArray &hash, const QString &text, qint64 timestamp);
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
};

// Not parented to the test object: slots recreate it directly.


void TestStorage::init()
{
    delete m_storage; // deleteLater() never runs without an event loop
    const QString path = m_dir.filePath(
        QStringLiteral("history-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
}

ClipboardRecord TestStorage::makeRecord(const QByteArray &hash, const QString &text,
                                        qint64 timestamp)
{
    ClipboardRecord record;
    record.hash = hash;
    record.type = ContentType::Text;
    record.textData = text;
    record.preview = text.left(40);
    record.timestamp = timestamp;
    record.sizeBytes = text.size();
    record.sourceApp = QStringLiteral("tester");
    return record;
}

void TestStorage::insertAndFetch()
{
    const qint64 id = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("h1"), QStringLiteral("hello"), 1000));
    QVERIFY(id > 0);

    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(id, &full));
    QCOMPARE(full.textData, QStringLiteral("hello"));
    QCOMPARE(full.type, ContentType::Text);
    QVERIFY(full.hash == QByteArrayLiteral("h1"));

    bool hasMore = true;
    const auto page = m_storage->fetchPage(FilterSpec{}, {}, 10, &hasMore);
    QCOMPARE(page.size(), 1);
    QCOMPARE(page.first().id, id);
    QCOMPARE(page.first().preview, QStringLiteral("hello"));
    QVERIFY(!page.first().hasBlob); // summary record
    QVERIFY(!hasMore);
}

void TestStorage::dedupUpdatesTimestamp()
{
    const qint64 first = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("dup"), QStringLiteral("same"), 1000));
    bool updated = false;
    const qint64 second =
        m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("dup"), QStringLiteral("same"), 9000), &updated);
    QCOMPARE(first, second);
    QVERIFY(updated);
    QCOMPARE(m_storage->stats().entryCount, qint64(1));

    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(first, &full));
    QCOMPARE(full.timestamp, qint64(9000));
    QCOMPARE(full.useCount, 1);
}

void TestStorage::pagination()
{
    for (int i = 0; i < 25; ++i)
        m_storage->insertOrUpdate(
            makeRecord(QByteArrayLiteral("p") + QByteArray::number(i),
                       QStringLiteral("entry %1").arg(i), 1000 + i));
    QCOMPARE(m_storage->stats().entryCount, qint64(25));

    bool hasMore = false;
    auto page = m_storage->fetchPage(FilterSpec{}, {}, 10, &hasMore);
    QCOMPARE(page.size(), 10);
    QVERIFY(hasMore);
    QCOMPARE(page.first().preview, QStringLiteral("entry 24")); // newest first

    PageCursor cursor{true, page.last().timestamp, page.last().id};
    page = m_storage->fetchPage(FilterSpec{}, cursor, 10, &hasMore);
    QCOMPARE(page.size(), 10);
    QVERIFY(hasMore);

    cursor = PageCursor{true, page.last().timestamp, page.last().id};
    page = m_storage->fetchPage(FilterSpec{}, cursor, 10, &hasMore);
    QCOMPARE(page.size(), 5);
    QVERIFY(!hasMore);

    // fetchAll walks everything.
    QCOMPARE(m_storage->fetchAll(FilterSpec{}).size(), 25);
}

void TestStorage::filters()
{
    m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("f1"), QStringLiteral("kde plasma"), 1000));
    m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("f2"), QStringLiteral("gnome shell"), 2000));

    ClipboardRecord image;
    image.hash = QByteArrayLiteral("f3");
    image.type = ContentType::Image;
    image.blobData = QByteArrayLiteral("PNGDATA");
    image.hasBlob = true;
    image.timestamp = 3000;
    image.preview = QStringLiteral("Image 10x10");
    image.sourceApp = QStringLiteral("spectacle");
    m_storage->insertOrUpdate(image);

    FilterSpec filter;
    filter.searchText = QStringLiteral("plasma");
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 1);

    filter = FilterSpec{};
    filter.contentType = int(ContentType::Image);
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 1);

    filter = FilterSpec{};
    filter.fromMs = 2500;
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 1);

    filter = FilterSpec{};
    filter.sourceApp = QStringLiteral("tester");
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 2);

    // LIKE metacharacters are escaped.
    filter = FilterSpec{};
    filter.searchText = QStringLiteral("%");
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 0);

    QVERIFY(m_storage->sourceApps().contains(QStringLiteral("spectacle")));
}

void TestStorage::pinnedAndRemove()
{
    const qint64 pinnedId =
        m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("pin"), QStringLiteral("keep me"), 1000));
    m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("pin2"), QStringLiteral("remove me"), 2000));
    QVERIFY(m_storage->setPinned(pinnedId, true));
    QCOMPARE(m_storage->stats().pinnedCount, qint64(1));

    QCOMPARE(m_storage->clearHistory(false), 1); // only the unpinned one
    QCOMPARE(m_storage->stats().entryCount, qint64(1));
    QCOMPARE(m_storage->clearHistory(true), 1);
    QCOMPARE(m_storage->stats().entryCount, qint64(0));
}

void TestStorage::diskCap()
{
    for (int i = 0; i < 10; ++i) {
        ClipboardRecord record =
            makeRecord(QByteArrayLiteral("c") + QByteArray::number(i),
                       QStringLiteral("0123456789"), 1000 + i); // 10 bytes each
        record.sizeBytes = 10;
        m_storage->insertOrUpdate(record);
    }
    // Cap of 55 bytes => keep ~6 of 10 (oldest non-pinned removed).
    QCOMPARE(m_storage->stats().totalBytes, qint64(100));
    m_storage->enforceDiskCap(55);
    QVERIFY(m_storage->stats().totalBytes <= 55);
    QVERIFY(m_storage->stats().entryCount > 0);
}

void TestStorage::maxEntriesCap()
{
    for (int i = 0; i < 10; ++i)
        m_storage->insertOrUpdate(
            makeRecord(QByteArrayLiteral("m") + QByteArray::number(i),
                       QStringLiteral("entry"), 1000 + i));
    QCOMPARE(m_storage->stats().entryCount, qint64(10));

    // Keep only the 3 newest; oldest non-pinned rows go first.
    QCOMPARE(m_storage->enforceMaxEntries(3), 7);
    QCOMPARE(m_storage->stats().entryCount, qint64(3));

    // Pinned entries survive even when the cap is already full.
    const qint64 pinnedId =
        m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("p"), QStringLiteral("pinned"), 100));
    QVERIFY(m_storage->setPinned(pinnedId, true));
    QCOMPARE(m_storage->stats().entryCount, qint64(4));
    QCOMPARE(m_storage->stats().pinnedCount, qint64(1));
    QCOMPARE(m_storage->enforceMaxEntries(3), 1); // drop oldest unpinned
    QCOMPARE(m_storage->stats().entryCount, qint64(3));
    QCOMPARE(m_storage->stats().pinnedCount, qint64(1));

    // Unlimited / invalid caps are no-ops.
    QCOMPARE(m_storage->enforceMaxEntries(0), 0);
    QCOMPARE(m_storage->enforceMaxEntries(-5), 0);
    QCOMPARE(m_storage->stats().entryCount, qint64(3));
}

void TestStorage::touchEntryBump()
{
    const qint64 id = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("touch"), QStringLiteral("touch me"), 1000));
    QVERIFY(id != 0);

    ClipboardRecord before;
    QVERIFY(m_storage->fetchFull(id, &before));
    QCOMPARE(before.useCount, 0);

    QSignalSpy touchedSpy(m_storage, &StorageManager::entryTouched);
    QTest::qWait(2); // timestamp resolution is milliseconds
    QVERIFY(m_storage->touchEntry(id));
    QCOMPARE(touchedSpy.count(), 1);
    QCOMPARE(touchedSpy.first().first().toLongLong(), id);

    ClipboardRecord after;
    QVERIFY(m_storage->fetchFull(id, &after));
    QVERIFY(after.timestamp > before.timestamp);
    QCOMPARE(after.useCount, before.useCount + 1);

    // Unknown ids fail without emitting.
    QVERIFY(!m_storage->touchEntry(999999));
    QCOMPARE(touchedSpy.count(), 1);
    QVERIFY(!m_storage->touchEntry(0));
    QCOMPARE(touchedSpy.count(), 1);
}

void TestStorage::emitsHistorySignals()
{
    QSignalSpy addedSpy(m_storage, &StorageManager::entryAdded);
    QSignalSpy touchedSpy(m_storage, &StorageManager::entryTouched);

    const qint64 id = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("signal"), QStringLiteral("signal entry"), 1000));
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(touchedSpy.count(), 0);
    QCOMPARE(addedSpy.at(0).at(0).toLongLong(), id);

    const qint64 duplicateId = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("signal"), QStringLiteral("signal entry"), 2000));
    QCOMPARE(duplicateId, id);
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(touchedSpy.count(), 1);
    QCOMPARE(touchedSpy.at(0).at(0).toLongLong(), id);
}

void TestStorage::modelRefreshesOnHistoryChanges()
{
    ClipboardListModel model(m_storage);
    QCOMPARE(model.rowCount(), 0);

    const qint64 id = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("model"), QStringLiteral("first"), 1000));
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.idAt(0), id);
    QCOMPARE(model.recordAt(0).preview, QStringLiteral("first"));

    m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("model"), QStringLiteral("first"), 2000));
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.recordAt(0).timestamp, qint64(2000));
}

void TestStorage::binaryPayloadAndMetadata()
{
    ClipboardRecord image;
    image.hash = QByteArrayLiteral("image");
    image.type = ContentType::Image;
    image.blobData = QByteArray::fromHex(QByteArrayLiteral("89504e47"));
    image.hasBlob = true;
    image.preview = QStringLiteral("Image 1x1");
    image.sizeBytes = image.blobData.size();
    image.timestamp = 1234;
    image.sourceApp = QStringLiteral("spectacle");
    image.sourceWindow = QStringLiteral("Screenshot");

    const qint64 id = m_storage->insertOrUpdate(image);
    QVERIFY(id > 0);
    QCOMPARE(m_storage->stats().imageCount, qint64(1));

    const auto summary = m_storage->fetchPage(FilterSpec{}, {}, 10);
    QCOMPARE(summary.size(), 1);
    QVERIFY(summary.first().hasBlob);

    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(id, &full));
    QCOMPARE(full.type, ContentType::Image);
    QCOMPARE(full.blobData, image.blobData);
    QCOMPARE(full.sourceApp, image.sourceApp);
    QCOMPARE(full.sourceWindow, image.sourceWindow);
}

void TestStorage::searchEscapesLikeCharacters()
{
    m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("percent"), QStringLiteral("literal % marker"), 1000));
    m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("underscore"), QStringLiteral("literal _ marker"), 2000));
    m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("slash"), QStringLiteral("literal \\ marker"), 3000));

    FilterSpec filter;
    filter.searchText = QStringLiteral("%");
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 1);
    filter.searchText = QStringLiteral("_");
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 1);
    filter.searchText = QStringLiteral("\\");
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 1);
}

void TestStorage::diskCapPreservesPinnedEntries()
{
    ClipboardRecord pinned = makeRecord(QByteArrayLiteral("pinned"), QStringLiteral("1234567890"), 1000);
    pinned.sizeBytes = 10;
    const qint64 pinnedId = m_storage->insertOrUpdate(pinned);
    QVERIFY(m_storage->setPinned(pinnedId, true));

    for (int i = 0; i < 2; ++i) {
        ClipboardRecord record = makeRecord(QByteArrayLiteral("cap-") + QByteArray::number(i),
                                            QStringLiteral("1234567890"), 2000 + i);
        record.sizeBytes = 10;
        m_storage->insertOrUpdate(record);
    }

    m_storage->enforceDiskCap(15);
    QVERIFY(m_storage->stats().totalBytes <= 15);
    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(pinnedId, &full));
    QVERIFY(full.pinned);
}

void TestStorage::sensitiveFilterAndStats()
{
    ClipboardRecord normal =
        makeRecord(QByteArrayLiteral("s-normal"), QStringLiteral("nothing secret"), 1000);
    m_storage->insertOrUpdate(normal);

    ClipboardRecord secret = makeRecord(QByteArrayLiteral("s-secret"), QStringLiteral("password=hunter2"), 2000);
    secret.sensitive = true;
    m_storage->insertOrUpdate(secret);

    QCOMPARE(m_storage->stats().sensitiveCount, qint64(1));

    FilterSpec filter;
    QCOMPARE(m_storage->fetchPage(filter, {}, 10).size(), 2);
    filter.sensitiveOnly = true;
    const auto page = m_storage->fetchPage(filter, {}, 10);
    QCOMPARE(page.size(), 1);
    QVERIFY(page.first().sensitive);
    QVERIFY(!filter.isTrivial());
}

void TestStorage::expireRules()
{
    const qint64 oldText = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("e-text"), QStringLiteral("old text"), 1000));
    ClipboardRecord oldImage;
    oldImage.hash = QByteArrayLiteral("e-image");
    oldImage.type = ContentType::Image;
    oldImage.blobData = QByteArrayLiteral("PNG");
    oldImage.hasBlob = true;
    oldImage.preview = QStringLiteral("Image");
    oldImage.timestamp = 2000;
    oldImage.sourceApp = QStringLiteral("spectacle");
    m_storage->insertOrUpdate(oldImage);

    ClipboardRecord pinned = makeRecord(QByteArrayLiteral("e-pinned"), QStringLiteral("keep pinned"), 3000);
    const qint64 pinnedId = m_storage->insertOrUpdate(pinned);
    QVERIFY(m_storage->setPinned(pinnedId, true));

    m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("e-new"), QStringLiteral("fresh"), 90000));

    QCOMPARE(m_storage->stats().entryCount, qint64(4));

    // Expire everything older than 50s, keep pinned.
    QCOMPARE(m_storage->expireEntries(50000, -1, QString(), true), 2);
    QCOMPARE(m_storage->stats().entryCount, qint64(2));
    ClipboardRecord gone;
    QVERIFY(!m_storage->fetchFull(oldText, &gone)); // row expired

    // Only images older than 50s from spectacle*.
    ClipboardRecord oldSpectacle;
    oldSpectacle.hash = QByteArrayLiteral("e-old-spec");
    oldSpectacle.type = ContentType::Image;
    oldSpectacle.timestamp = 4000;
    oldSpectacle.sourceApp = QStringLiteral("spectacle");
    oldSpectacle.preview = QStringLiteral("Image");
    m_storage->insertOrUpdate(oldSpectacle);
    ClipboardRecord oldFirefox;
    oldFirefox.hash = QByteArrayLiteral("e-old-fx");
    oldFirefox.type = ContentType::Image;
    oldFirefox.timestamp = 4500;
    oldFirefox.sourceApp = QStringLiteral("firefox");
    oldFirefox.preview = QStringLiteral("Image");
    m_storage->insertOrUpdate(oldFirefox);
    ClipboardRecord freshImage;
    freshImage.hash = QByteArrayLiteral("e-fresh-img");
    freshImage.type = ContentType::Image;
    freshImage.timestamp = 60000;
    freshImage.sourceApp = QStringLiteral("spectacle");
    freshImage.preview = QStringLiteral("Image");
    m_storage->insertOrUpdate(freshImage);
    QCOMPARE(m_storage->stats().entryCount, qint64(5));

    // Only the old spectacle image qualifies: type and app wildcard both apply.
    QCOMPARE(m_storage->expireEntries(50000, int(ContentType::Image), QStringLiteral("spectacle*"), true), 1);

    // keepPinned=false also removes old pinned rows (and the old firefox image).
    QCOMPARE(m_storage->expireEntries(50000, -1, QString(), false), 2);
    ClipboardRecord full;
    QVERIFY(!m_storage->fetchFull(pinnedId, &full));
    QCOMPARE(m_storage->stats().entryCount, qint64(2)); // fresh text + fresh image
}

void TestStorage::bulkRemovalAndSignal()
{
    const qint64 id1 = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("b1"), QStringLiteral("bulk 1"), 1000));
    const qint64 id2 = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("b2"), QStringLiteral("bulk 2"), 2000));
    const qint64 id3 = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("b3"), QStringLiteral("bulk 3"), 3000));
    QCOMPARE(m_storage->stats().entryCount, qint64(3));

    QSignalSpy spy(m_storage, &IClipboardStorage::entriesRemoved);

    // Remove empty list is no-op
    QCOMPARE(m_storage->removeEntries({}), 0);
    QCOMPARE(spy.count(), 0);

    // Bulk remove id1 and id3
    const int removed = m_storage->removeEntries({id1, id3});
    QCOMPARE(removed, 2);
    QCOMPARE(m_storage->stats().entryCount, qint64(1));
    QCOMPARE(spy.count(), 1);

    const QList<qint64> removedIds = spy.first().at(0).value<QList<qint64>>();
    QCOMPARE(removedIds.size(), 2);
    QVERIFY(removedIds.contains(id1));
    QVERIFY(removedIds.contains(id3));

    ClipboardRecord rec;
    QVERIFY(!m_storage->fetchFull(id1, &rec));
    QVERIFY(m_storage->fetchFull(id2, &rec));
    QVERIFY(!m_storage->fetchFull(id3, &rec));
}

void TestStorage::clearHistorySelectiveAndTotal()
{
    const qint64 unpinned1 = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("u1"), QStringLiteral("unpinned 1"), 1000));
    const qint64 unpinned2 = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("u2"), QStringLiteral("unpinned 2"), 2000));
    const qint64 pinned1 = m_storage->insertOrUpdate(makeRecord(QByteArrayLiteral("p1"), QStringLiteral("pinned 1"), 3000));
    m_storage->setPinned(pinned1, true);

    QCOMPARE(m_storage->stats().entryCount, qint64(3));
    QCOMPARE(m_storage->stats().pinnedCount, qint64(1));

    QSignalSpy resetSpy(m_storage, &IClipboardStorage::storageReset);

    // Clear without including pinned
    const int removedUnpinned = m_storage->clearHistory(false);
    QCOMPARE(removedUnpinned, 2);
    QCOMPARE(m_storage->stats().entryCount, qint64(1));
    QCOMPARE(m_storage->stats().pinnedCount, qint64(1));
    QCOMPARE(resetSpy.count(), 1);

    ClipboardRecord rec;
    QVERIFY(!m_storage->fetchFull(unpinned1, &rec));
    QVERIFY(!m_storage->fetchFull(unpinned2, &rec));
    QVERIFY(m_storage->fetchFull(pinned1, &rec));

    // Clear including pinned
    const int removedAll = m_storage->clearHistory(true);
    QCOMPARE(removedAll, 1);
    QCOMPARE(m_storage->stats().entryCount, qint64(0));
    QCOMPARE(resetSpy.count(), 2);
    QVERIFY(!m_storage->fetchFull(pinned1, &rec));
}

void TestStorage::ocrTextStorageAndStats()
{
    ClipboardRecord image;
    image.hash = QByteArrayLiteral("ocr_stats");
    image.type = ContentType::Image;
    image.blobData = QByteArrayLiteral("imgdata");
    image.hasBlob = true;
    image.preview = QStringLiteral("Image");
    image.timestamp = 1000;

    const qint64 id = m_storage->insertOrUpdate(image);
    QVERIFY(id > 0);
    QCOMPARE(m_storage->stats().imageCount, qint64(1));
    QCOMPARE(m_storage->stats().ocrCount, qint64(0));

    // Set OCR text
    QVERIFY(m_storage->setOcrText(id, QStringLiteral("Extracted text from image")));
    QCOMPARE(m_storage->stats().ocrCount, qint64(1));

    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(id, &full));
    QCOMPARE(full.ocrText, QStringLiteral("Extracted text from image"));

    // Setting OCR text on non-existent entry returns false
    QVERIFY(!m_storage->setOcrText(99999, QStringLiteral("none")));
}

void TestStorage::sourceAppsDistinctListing()
{
    ClipboardRecord r1 = makeRecord(QByteArrayLiteral("s1"), QStringLiteral("t1"), 1000);
    r1.sourceApp = QStringLiteral("Kate");
    m_storage->insertOrUpdate(r1);

    ClipboardRecord r2 = makeRecord(QByteArrayLiteral("s2"), QStringLiteral("t2"), 2000);
    r2.sourceApp = QStringLiteral("firefox");
    m_storage->insertOrUpdate(r2);

    ClipboardRecord r3 = makeRecord(QByteArrayLiteral("s3"), QStringLiteral("t3"), 3000);
    r3.sourceApp = QStringLiteral("kate"); // case insensitive duplicate
    m_storage->insertOrUpdate(r3);

    ClipboardRecord r4 = makeRecord(QByteArrayLiteral("s4"), QStringLiteral("t4"), 4000);
    r4.sourceApp = QString(); // empty source app
    m_storage->insertOrUpdate(r4);

    const QStringList apps = m_storage->sourceApps();
    QVERIFY(!apps.contains(QString()));
    // Ordered by COLLATE NOCASE
    QVERIFY(apps.contains(QStringLiteral("firefox"), Qt::CaseInsensitive));
    QVERIFY(apps.contains(QStringLiteral("kate"), Qt::CaseInsensitive));
}

void TestStorage::rejectsInvalidRequests()
{
    bool hasMore = true;
    QVERIFY(m_storage->fetchPage(FilterSpec{}, {}, 0, &hasMore).isEmpty());
    QVERIFY(!hasMore);
    hasMore = true;
    QVERIFY(m_storage->fetchPage(FilterSpec{}, {}, -1, &hasMore).isEmpty());
    QVERIFY(!hasMore);

    ClipboardRecord record;
    QVERIFY(!m_storage->fetchFull(99999, &record));
    QVERIFY(!m_storage->fetchFull(99999, nullptr));
    QVERIFY(!m_storage->remove(99999));
    QVERIFY(!m_storage->setPinned(99999, true));
    QVERIFY(!m_storage->setOcrText(99999, QStringLiteral("missing")));
    QCOMPARE(m_storage->removeEntries({99999}), 0);
    QCOMPARE(m_storage->expireEntries(0, -1, QString(), true), 0);
    QCOMPARE(m_storage->expireEntries(-1, -1, QString(), false), 0);
    QCOMPARE(m_storage->enforceDiskCap(0), 0);
    QCOMPARE(m_storage->enforceDiskCap(-1), 0);
}

void TestStorage::appliesInclusiveTimeBoundsAndCombinedFilters()
{
    ClipboardRecord before = makeRecord(QByteArrayLiteral("bounds-before"), QStringLiteral("before"), 1000);
    m_storage->insertOrUpdate(before);

    ClipboardRecord matching = makeRecord(QByteArrayLiteral("bounds-match"), QStringLiteral("matching"), 2000);
    matching.pinned = true;
    matching.sensitive = true;
    m_storage->insertOrUpdate(matching);

    ClipboardRecord different = makeRecord(QByteArrayLiteral("bounds-different"), QStringLiteral("different"), 3000);
    different.type = ContentType::Image;
    different.sourceApp = QStringLiteral("other-app");
    m_storage->insertOrUpdate(different);

    FilterSpec filter;
    filter.fromMs = 2000;
    filter.toMs = 2000;
    filter.contentType = int(ContentType::Text);
    filter.sourceApp = QStringLiteral("tester");
    filter.pinnedOnly = true;
    filter.sensitiveOnly = true;

    const auto page = m_storage->fetchPage(filter, {}, 10);
    QCOMPARE(page.size(), 1);
    QCOMPARE(page.first().preview, QStringLiteral("matching"));
    QVERIFY(!filter.isTrivial());
}

void TestStorage::paginatesEntriesWithEqualTimestamps()
{
    const qint64 first = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("same-time-1"), QStringLiteral("one"), 5000));
    const qint64 second = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("same-time-2"), QStringLiteral("two"), 5000));
    const qint64 third = m_storage->insertOrUpdate(
        makeRecord(QByteArrayLiteral("same-time-3"), QStringLiteral("three"), 5000));

    bool hasMore = false;
    const auto firstPage = m_storage->fetchPage(FilterSpec{}, {}, 2, &hasMore);
    QCOMPARE(firstPage.size(), 2);
    QVERIFY(hasMore);
    QCOMPARE(firstPage.at(0).id, third);
    QCOMPARE(firstPage.at(1).id, second);

    const PageCursor cursor{true, firstPage.last().timestamp, firstPage.last().id};
    const auto secondPage = m_storage->fetchPage(FilterSpec{}, cursor, 2, &hasMore);
    QCOMPARE(secondPage.size(), 1);
    QVERIFY(!hasMore);
    QCOMPARE(secondPage.first().id, first);
}

void TestStorage::clearsOcrTextAndReturnsFullPayloads()
{
    ClipboardRecord record = makeRecord(QByteArrayLiteral("full-payload"), QStringLiteral("full text"), 1000);
    const qint64 id = m_storage->insertOrUpdate(record);
    QVERIFY(m_storage->setOcrText(id, QStringLiteral("recognized")));
    QCOMPARE(m_storage->stats().ocrCount, qint64(1));

    QVERIFY(m_storage->setOcrText(id, QString()));
    QCOMPARE(m_storage->stats().ocrCount, qint64(0));

    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(id, &full));
    QCOMPARE(full.textData, QStringLiteral("full text"));
    QCOMPARE(full.ocrText, QString());

    const auto allFull = m_storage->fetchAllFull(FilterSpec{});
    QCOMPARE(allFull.size(), 1);
    QCOMPARE(allFull.first().id, id);
    QCOMPARE(allFull.first().textData, QStringLiteral("full text"));
}

QTEST_GUILESS_MAIN(TestStorage)
#include "tst_storage.moc"
