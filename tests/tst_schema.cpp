#include <QtTest>

#include "BookmarkManager.h"
#include "DatabaseSchema.h"
#include "SearchEngine.h"
#include "StorageManager.h"

#include <QSqlQuery>
#include <QTemporaryDir>

class TestSchema : public QObject
{
    Q_OBJECT

private slots:
    void ensureIsIdempotentAndEnablesForeignKeys();
    void dataAndSearchSurviveReopening();
    void foreignKeysCascadeMemberships();
    void migratesLegacyOcrAndFtsSchema();
};

void TestSchema::ensureIsIdempotentAndEnablesForeignKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString connectionName = QStringLiteral("schema-test");
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(dir.filePath(QStringLiteral("schema.db")));
    QVERIFY(db.open());

    QVERIFY(DatabaseSchema::ensure(db));
    QVERIFY(DatabaseSchema::ensure(db));

    QSqlQuery pragma(db);
    QVERIFY(pragma.exec(QStringLiteral("PRAGMA foreign_keys")));
    QVERIFY(pragma.next());
    QCOMPARE(pragma.value(0).toInt(), 1);

    QSqlQuery tables(db);
    QVERIFY(tables.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type IN ('table', 'shadow table')")));
    QSet<QString> names;
    while (tables.next())
        names.insert(tables.value(0).toString());

    QVERIFY(names.contains(QStringLiteral("entries")));
    QVERIFY(names.contains(QStringLiteral("groups")));
    QVERIFY(names.contains(QStringLiteral("entry_groups")));
    QVERIFY(names.contains(QStringLiteral("snippets")));
    QVERIFY(names.contains(QStringLiteral("entries_fts")));

    tables = QSqlQuery();
    pragma = QSqlQuery();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

void TestSchema::dataAndSearchSurviveReopening()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("reopen.db"));
    qint64 id = 0;

    {
        StorageManager storage(path);
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("reopen-hash");
        record.type = ContentType::Text;
        record.textData = QStringLiteral("persisted clipboard payload");
        record.preview = QStringLiteral("persisted clipboard payload");
        record.timestamp = 1000;
        record.sizeBytes = record.textData.toUtf8().size();
        id = storage.insertOrUpdate(record);
        QVERIFY(id > 0);
        QVERIFY(storage.setOcrText(id, QStringLiteral("recognized persisted text")));
    }

    {
        StorageManager storage(path);
        ClipboardRecord restored;
        QVERIFY(storage.fetchFull(id, &restored));
        QCOMPARE(restored.textData, QStringLiteral("persisted clipboard payload"));
        QCOMPARE(restored.ocrText, QStringLiteral("recognized persisted text"));
        QCOMPARE(storage.stats().ocrCount, qint64(1));

        FilterSpec textFilter;
        textFilter.searchText = QStringLiteral("persisted");
        const auto matches = storage.fetchAll(textFilter);
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.first().id, id);

        FilterSpec ocrFilter;
        ocrFilter.searchText = QStringLiteral("recognized");
        QCOMPARE(storage.fetchAll(ocrFilter).size(), 1);

        QVERIFY(storage.remove(id));
        QCOMPARE(storage.stats().entryCount, qint64(0));
        QVERIFY(storage.fetchAll(textFilter).isEmpty());
    }
}

void TestSchema::foreignKeysCascadeMemberships()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("foreign-keys.db")));
    BookmarkManager bookmarks(storage.database());

    ClipboardRecord first;
    first.hash = QByteArrayLiteral("foreign-entry-1");
    first.textData = QStringLiteral("first");
    first.preview = first.textData;
    first.timestamp = 1;
    const qint64 firstId = storage.insertOrUpdate(first);
    const qint64 group = bookmarks.createGroup(QStringLiteral("Cascade"));
    QVERIFY(firstId > 0);
    QVERIFY(group > 0);
    QVERIFY(bookmarks.assignEntry(firstId, group));

    QVERIFY(storage.remove(firstId));
    QCOMPARE(bookmarks.entryIdsForGroup(group), QList<qint64>{});

    ClipboardRecord second;
    second.hash = QByteArrayLiteral("foreign-entry-2");
    second.textData = QStringLiteral("second");
    second.preview = second.textData;
    second.timestamp = 2;
    const qint64 secondId = storage.insertOrUpdate(second);
    QVERIFY(bookmarks.assignEntry(secondId, group));

    QVERIFY(bookmarks.deleteGroup(group));
    ClipboardRecord stillPresent;
    QVERIFY(storage.fetchFull(secondId, &stillPresent));
    QCOMPARE(bookmarks.groupIdsForEntry(secondId), QList<qint64>{});
}

void TestSchema::migratesLegacyOcrAndFtsSchema()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.db"));
    const QString connectionName = QStringLiteral("legacy-schema-test");

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE entries ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "timestamp_ms INTEGER NOT NULL,"
            "content_type INTEGER NOT NULL,"
            "content_hash TEXT NOT NULL UNIQUE,"
            "text_data TEXT, blob_data BLOB, preview TEXT,"
            "size_bytes INTEGER NOT NULL DEFAULT 0,"
            "pinned INTEGER NOT NULL DEFAULT 0,"
            "sensitive INTEGER NOT NULL DEFAULT 0,"
            "use_count INTEGER NOT NULL DEFAULT 0,"
            "source_app TEXT, source_window TEXT)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO entries(timestamp_ms, content_type, content_hash, text_data, preview, size_bytes) "
            "VALUES (1000, 0, 'legacy-hash', 'legacy searchable text', 'legacy searchable text', 23)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE entries_fts USING fts5(preview, text_data, content='entries', content_rowid='id', tokenize='unicode61')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO entries_fts(rowid, preview, text_data) "
            "VALUES (1, 'legacy searchable text', 'legacy searchable text')")));
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    StorageManager storage(path);
    ClipboardRecord restored;
    QVERIFY(storage.fetchFull(1, &restored));
    QCOMPARE(restored.hash, QByteArrayLiteral("legacy-hash"));
    QCOMPARE(restored.textData, QStringLiteral("legacy searchable text"));
    QCOMPARE(restored.ocrText, QString());

    FilterSpec filter;
    filter.searchText = QStringLiteral("legacy searchable");
    const auto matches = storage.fetchAll(filter);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().id, qint64(1));

    QSqlQuery columns(storage.database());
    QVERIFY(columns.exec(QStringLiteral("PRAGMA table_info(entries)")));
    bool hasOcrColumn = false;
    while (columns.next()) {
        if (columns.value(1).toString() == QStringLiteral("ocr_text")) {
            hasOcrColumn = true;
            break;
        }
    }
    QVERIFY(hasOcrColumn);
}

QTEST_GUILESS_MAIN(TestSchema)
#include "tst_schema.moc"
