#include "../src/app/EgoboardDbusAdaptor.h"
#include "../src/krunner/EntryRow.h"
#include "../src/krunner/RunnerActions.h"
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
    void searchNormalizesLimitsAndPreviewText();
    void pasteAcceptsAnyIdAndEmitsIt();
    // Richer results + per-match actions (KRunner plugin)
    void detailedRowsCarryMetadata();
    void detailedSearchNormalizesLimits();
    void rowCodecRoundTripsAndSurvivesGarbage();
    void rowCodecKeepsNewlinesAndStripsTabs();
    void previewReturnsTextAndIsCapped();
    void copyPinAndDeleteEmitForKnownEntries();
    void actionsRefuseUnknownEntries();
    void contentTypeIdsCoverAllTypes();
    void runnerActionProtocolMapsToDbusMethods();

private:
    static qint64 addEntry(StorageManager &storage, const QString &text, const QString &hash,
                           ContentType type = ContentType::Text);
};

qint64 TestKRunner::addEntry(StorageManager &storage, const QString &text, const QString &hash,
                            ContentType type)
{
    ClipboardRecord record;
    record.type = type;
    record.textData = text;
    record.preview = text;
    record.hash = hash.toUtf8();
    record.timestamp = 1000;
    record.sizeBytes = text.toUtf8().size();
    return storage.insertOrUpdate(record);
}

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

void TestKRunner::searchNormalizesLimitsAndPreviewText()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    for (int i = 0; i < 12; ++i) {
        ClipboardRecord record;
        record.type = ContentType::Text;
        record.textData = QStringLiteral("entry %1").arg(i);
        record.preview = record.textData;
        record.hash = QByteArrayLiteral("limit-") + QByteArray::number(i);
        record.timestamp = 1000 + i;
        QVERIFY(storage.insertOrUpdate(record) != 0);
    }

    EgoboardDbusAdaptor adaptor(&storage);
    QCOMPARE(adaptor.Search(QString(), 0).size(), 10);
    QCOMPARE(adaptor.Search(QString(), -5).size(), 10);
    QCOMPARE(adaptor.Search(QString(), 100).size(), 12);

    ClipboardRecord longPreview;
    longPreview.type = ContentType::Text;
    longPreview.textData = QStringLiteral("payload");
    longPreview.preview = QStringLiteral("a").repeated(250).insert(50, QLatin1Char('\n'));
    longPreview.hash = QByteArrayLiteral("long-preview");
    longPreview.timestamp = 5000;
    QVERIFY(storage.insertOrUpdate(longPreview) != 0);

    const QStringList rows = adaptor.Search(QStringLiteral("payload"), 1);
    QCOMPARE(rows.size(), 1);
    const QStringList parts = rows.first().split(QLatin1Char('\t'));
    QCOMPARE(parts.size(), 2);
    QVERIFY(parts.at(1).size() <= 200);
    QVERIFY(!parts.at(1).contains(QLatin1Char('\n')));
}

void TestKRunner::pasteAcceptsAnyIdAndEmitsIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    EgoboardDbusAdaptor adaptor(&storage);
    QSignalSpy spy(&adaptor, &EgoboardDbusAdaptor::pasteRequested);

    QVERIFY(adaptor.Paste(999999));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toLongLong(), qint64(999999));
}

void TestKRunner::detailedRowsCarryMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    ClipboardRecord record;
    record.type = ContentType::RichText;
    record.textData = QStringLiteral("<b>hello</b> world");
    record.preview = QStringLiteral("hello world");
    record.hash = QByteArrayLiteral("detailed-1");
    record.timestamp = 4242;
    record.sourceApp = QStringLiteral("Firefox");
    record.sourceWindow = QStringLiteral("Docs — Mozilla Firefox");
    record.pinned = false;
    const qint64 id = storage.insertOrUpdate(record);
    QVERIFY(id != 0);

    EgoboardDbusAdaptor adaptor(&storage);
    const QStringList rows = adaptor.SearchDetailed(QStringLiteral("hello"), 10);
    QCOMPARE(rows.size(), 1);
    const EntryRow row = EntryRow::decode(rows.first());
    QVERIFY(row.isValid());
    QCOMPARE(row.id, id);
    QCOMPARE(row.type, QStringLiteral("html"));
    QCOMPARE(row.sourceApp, QStringLiteral("Firefox"));
    QCOMPARE(row.sourceWindow, QStringLiteral("Docs — Mozilla Firefox"));
    QVERIFY(!row.pinned);
    QCOMPARE(row.timestamp, qint64(4242));
    QVERIFY(row.preview.contains(QStringLiteral("hello")));
    QCOMPARE(row.summary(), QStringLiteral("Firefox · html"));

    // The plain Search format stays exactly as it was: id<TAB>preview.
    const QStringList legacy = adaptor.Search(QStringLiteral("hello"), 10);
    QCOMPARE(legacy.size(), 1);
    QCOMPARE(legacy.first().split(QLatin1Char('\t')).size(), 2);
}

void TestKRunner::detailedSearchNormalizesLimits()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    for (int i = 0; i < 12; ++i)
        QVERIFY(addEntry(storage, QStringLiteral("entry %1").arg(i), QStringLiteral("d-%1").arg(i)) != 0);

    EgoboardDbusAdaptor adaptor(&storage);
    QCOMPARE(adaptor.SearchDetailed(QString(), 0).size(), 10);
    QCOMPARE(adaptor.SearchDetailed(QString(), -3).size(), 10);
    QCOMPARE(adaptor.SearchDetailed(QString(), 100).size(), 12);
    QCOMPARE(adaptor.SearchDetailed(QStringLiteral("entry 3"), 5).size(), 1);
}

void TestKRunner::rowCodecRoundTripsAndSurvivesGarbage()
{
    EntryRow original;
    original.id = 77;
    original.type = QStringLiteral("image");
    original.sourceApp = QStringLiteral("Spectacle");
    original.sourceWindow = QString();
    original.pinned = true;
    original.timestamp = 1712345678901;
    original.preview = QStringLiteral("screenshot");

    const EntryRow decoded = EntryRow::decode(original.encode());
    QCOMPARE(decoded.id, original.id);
    QCOMPARE(decoded.type, original.type);
    QCOMPARE(decoded.sourceApp, original.sourceApp);
    QCOMPARE(decoded.sourceWindow, original.sourceWindow);
    QCOMPARE(decoded.pinned, original.pinned);
    QCOMPARE(decoded.timestamp, original.timestamp);
    QCOMPARE(decoded.preview, original.preview);

    // Garbage and the plain two-field Search rows are rejected, not crashed on.
    QVERIFY(!EntryRow::decode(QString()).isValid());
    QVERIFY(!EntryRow::decode(QStringLiteral("only one field")).isValid());
    QVERIFY(!EntryRow::decode(QStringLiteral("77\tscreenshot")).isValid());
    QVERIFY(!EntryRow::decode(QStringLiteral("notanumber\ttext\ta\tb\t0\t1\tp")).isValid());

    // A future adaptor may append fields; the known ones must still decode.
    const EntryRow extended = EntryRow::decode(original.encode() + QStringLiteral("\textra"));
    QCOMPARE(extended.id, original.id);
    QVERIFY(extended.preview.startsWith(QStringLiteral("screenshot")));
}

void TestKRunner::rowCodecKeepsNewlinesAndStripsTabs()
{
    EntryRow row;
    row.id = 5;
    row.type = QStringLiteral("text");
    row.preview = QStringLiteral("first line\nsecond\tline\nthird");
    const EntryRow decoded = EntryRow::decode(row.encode());
    // Newlines are what KRunner renders (multi-line match)...
    QVERIFY(decoded.preview.contains(QLatin1Char('\n')));
    QCOMPARE(decoded.preview.count(QLatin1Char('\n')), 2);
    // ...while tabs would break the field split and are replaced.
    QVERIFY(!decoded.preview.contains(QLatin1Char('\t')));
    QCOMPARE(decoded.preview, QStringLiteral("first line\nsecond line\nthird"));

    // Long previews are capped so a huge entry cannot flood the match list.
    EntryRow longRow;
    longRow.id = 6;
    longRow.type = QStringLiteral("text");
    longRow.preview = QStringLiteral("x").repeated(1000);
    QCOMPARE(EntryRow::decode(longRow.encode()).preview.size(), EntryRow::kMaxPreviewChars);
}

void TestKRunner::previewReturnsTextAndIsCapped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    const qint64 id = addEntry(storage, QStringLiteral("the full text"), QStringLiteral("preview-1"));
    QVERIFY(id != 0);

    EgoboardDbusAdaptor adaptor(&storage);
    QCOMPARE(adaptor.Preview(id), QStringLiteral("the full text"));
    QVERIFY(adaptor.Preview(999999).isEmpty());
    QVERIFY(adaptor.Preview(0).isEmpty());

    const qint64 longId = addEntry(storage, QStringLiteral("y").repeated(2000), QStringLiteral("preview-2"));
    QCOMPARE(adaptor.Preview(longId).size(), EntryRow::kMaxPreviewChars);
}

void TestKRunner::copyPinAndDeleteEmitForKnownEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    const qint64 id = addEntry(storage, QStringLiteral("act on me"), QStringLiteral("actions-1"));
    QVERIFY(id != 0);

    EgoboardDbusAdaptor adaptor(&storage);
    QSignalSpy copySpy(&adaptor, &EgoboardDbusAdaptor::copyRequested);
    QSignalSpy pinSpy(&adaptor, &EgoboardDbusAdaptor::pinRequested);
    QSignalSpy deleteSpy(&adaptor, &EgoboardDbusAdaptor::deleteRequested);

    QVERIFY(adaptor.Copy(id));
    QCOMPARE(copySpy.count(), 1);
    QCOMPARE(copySpy.first().first().toLongLong(), id);

    QVERIFY(adaptor.Pin(id, true));
    QCOMPARE(pinSpy.count(), 1);
    QCOMPARE(pinSpy.first().at(0).toLongLong(), id);
    QCOMPARE(pinSpy.first().at(1).toBool(), true);
    QVERIFY(adaptor.Pin(id, false));
    QCOMPARE(pinSpy.count(), 2);
    QCOMPARE(pinSpy.last().at(1).toBool(), false);

    QVERIFY(adaptor.Delete(id));
    QCOMPARE(deleteSpy.count(), 1);
    QCOMPARE(deleteSpy.first().first().toLongLong(), id);
}

void TestKRunner::actionsRefuseUnknownEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("history.db")));
    EgoboardDbusAdaptor adaptor(&storage);
    QSignalSpy copySpy(&adaptor, &EgoboardDbusAdaptor::copyRequested);
    QSignalSpy pinSpy(&adaptor, &EgoboardDbusAdaptor::pinRequested);
    QSignalSpy deleteSpy(&adaptor, &EgoboardDbusAdaptor::deleteRequested);

    // Unlike Paste (which keeps working for scripts), the KRunner actions only
    // act on entries that exist, and say so.
    QVERIFY(!adaptor.Copy(123456));
    QVERIFY(!adaptor.Pin(123456, true));
    QVERIFY(!adaptor.Delete(123456));
    QVERIFY(!adaptor.Copy(0));
    QVERIFY(!adaptor.Copy(-7));
    QCOMPARE(copySpy.count(), 0);
    QCOMPARE(pinSpy.count(), 0);
    QCOMPARE(deleteSpy.count(), 0);
}

void TestKRunner::contentTypeIdsCoverAllTypes()
{
    QCOMPARE(EntryRow::contentTypeId(ContentType::Text), QStringLiteral("text"));
    QCOMPARE(EntryRow::contentTypeId(ContentType::RichText), QStringLiteral("html"));
    QCOMPARE(EntryRow::contentTypeId(ContentType::Image), QStringLiteral("image"));
    QCOMPARE(EntryRow::contentTypeId(ContentType::Files), QStringLiteral("files"));

    // The summary skips empty parts instead of leaving separators behind.
    EntryRow row;
    row.type = QStringLiteral("text");
    QCOMPARE(row.summary(), QStringLiteral("text"));
    row.sourceApp = QStringLiteral("Konsole");
    QCOMPARE(row.summary(), QStringLiteral("Konsole · text"));
}

void TestKRunner::runnerActionProtocolMapsToDbusMethods()
{
    // The ids the KRunner plugin hands back must map onto the adaptor's methods
    // (this is the contract between the two libraries).
    QCOMPARE(RunnerActions::id(RunnerActions::Kind::Paste), QStringLiteral("egoboard-paste"));
    QCOMPARE(RunnerActions::id(RunnerActions::Kind::Copy), QStringLiteral("egoboard-copy"));
    QCOMPARE(RunnerActions::id(RunnerActions::Kind::Pin), QStringLiteral("egoboard-pin"));
    QCOMPARE(RunnerActions::id(RunnerActions::Kind::Unpin), QStringLiteral("egoboard-unpin"));
    QCOMPARE(RunnerActions::id(RunnerActions::Kind::Delete), QStringLiteral("egoboard-delete"));

    QCOMPARE(RunnerActions::dbusMethod(RunnerActions::Kind::Paste), QStringLiteral("Paste"));
    QCOMPARE(RunnerActions::dbusMethod(RunnerActions::Kind::Copy), QStringLiteral("Copy"));
    QCOMPARE(RunnerActions::dbusMethod(RunnerActions::Kind::Pin), QStringLiteral("Pin"));
    QCOMPARE(RunnerActions::dbusMethod(RunnerActions::Kind::Unpin), QStringLiteral("Pin"));
    QCOMPARE(RunnerActions::dbusMethod(RunnerActions::Kind::Delete), QStringLiteral("Delete"));

    // Only Pin carries the boolean; Unpin sends the same method with false.
    QVERIFY(RunnerActions::pinnedFlag(RunnerActions::Kind::Pin));
    QVERIFY(!RunnerActions::pinnedFlag(RunnerActions::Kind::Unpin));
    QVERIFY(!RunnerActions::pinnedFlag(RunnerActions::Kind::Delete));

    QCOMPARE(RunnerActions::kindForId(QStringLiteral("egoboard-delete")),
             RunnerActions::Kind::Delete);
    QCOMPARE(RunnerActions::kindForId(QStringLiteral("egoboard-unpin")),
             RunnerActions::Kind::Unpin);
    // Unknown or missing ids paste, which is what plain Enter does.
    QCOMPARE(RunnerActions::kindForId(QString()), RunnerActions::Kind::Paste);
    QCOMPARE(RunnerActions::kindForId(QStringLiteral("something-else")),
             RunnerActions::Kind::Paste);

    // Every id is distinct and every kind is covered exactly once.
    QSet<QString> ids;
    for (RunnerActions::Kind kind : RunnerActions::all())
        ids.insert(RunnerActions::id(kind));
    QCOMPARE(ids.size(), RunnerActions::all().size());
    QCOMPARE(RunnerActions::all().size(), 5);
}

QTEST_GUILESS_MAIN(TestKRunner)
#include "tst_krunner.moc"
