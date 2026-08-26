#include "../src/app/EgoboardDbusAdaptor.h"
#include "StorageManager.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestKRunner : public QObject {
    Q_OBJECT
private slots:
    void searchReturnsIdTabPreview();
    void searchRespectsLimit();
    void pasteEmitsSignal();
    void pingReturnsSame();
};

void TestKRunner::searchReturnsIdTabPreview()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    ClipboardRecord r;
    r.type = ContentType::Text;
    r.textData = QStringLiteral("hello world");
    r.preview = QStringLiteral("hello world");
    r.hash = QByteArrayLiteral("kr1");
    r.timestamp = 1000;
    r.sizeBytes = 11;
    QVERIFY(storage.insertOrUpdate(r) != 0);

    EgoboardDbusAdaptor adaptor(&storage);
    const QStringList rows = adaptor.Search(QStringLiteral("hello"), 10);
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.first().contains(QLatin1Char('\t')));
    const QStringList parts = rows.first().split(QLatin1Char('\t'));
    QCOMPARE(parts.size(), 2);
    QVERIFY(!parts[0].isEmpty());
    QVERIFY(parts[1].contains(QStringLiteral("hello")));
}

void TestKRunner::searchRespectsLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    for (int i = 0; i < 5; ++i) {
        ClipboardRecord r;
        r.type = ContentType::Text;
        r.textData = QStringLiteral("item %1").arg(i);
        r.preview = r.textData;
        r.hash = QByteArray::number(i + 100);
        r.timestamp = 1000 + i;
        r.sizeBytes = r.textData.size();
        QVERIFY(storage.insertOrUpdate(r) != 0);
    }
    EgoboardDbusAdaptor adaptor(&storage);
    QCOMPARE(adaptor.Search(QString(), 2).size(), 2);
    QCOMPARE(adaptor.Search(QString(), 50).size(), 5);
}

void TestKRunner::pasteEmitsSignal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    ClipboardRecord r;
    r.type = ContentType::Text;
    r.textData = QStringLiteral("paste me");
    r.preview = QStringLiteral("paste me");
    r.hash = QByteArrayLiteral("krpaste");
    r.timestamp = 1000;
    r.sizeBytes = 8;
    const qint64 id = storage.insertOrUpdate(r);
    EgoboardDbusAdaptor adaptor(&storage);
    QSignalSpy spy(&adaptor, &EgoboardDbusAdaptor::pasteRequested);
    QVERIFY(adaptor.Paste(id));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toLongLong(), id);
}

void TestKRunner::pingReturnsSame()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    EgoboardDbusAdaptor adaptor(&storage);
    QCOMPARE(adaptor.Ping(42), 42);
}

QTEST_GUILESS_MAIN(TestKRunner)
#include "tst_krunner.moc"
