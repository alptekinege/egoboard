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
    void emitsHistorySignals();

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

QTEST_GUILESS_MAIN(TestStorage)
#include "tst_storage.moc"
