#include <QtTest>

#include "BookmarkManager.h"
#include "DatabaseSchema.h"
#include "SearchEngine.h"
#include "StorageManager.h"

#include <QFile>
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
    void createsPerformanceIndexes();
    void normalizesLegacyBlobHashesAndDedupes();
    void dedupesCaseInsensitiveSavedSearches();
    void quickCheckReportsHealthyDatabase();
    void rebuildSearchIndexRepairsMissingIndex();
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

void TestSchema::createsPerformanceIndexes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString connectionName = QStringLiteral("schema-index-test");
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(dir.filePath(QStringLiteral("indexes.db")));
    QVERIFY(db.open());
    QVERIFY(DatabaseSchema::ensure(db));

    QSqlQuery indexes(db);
    QVERIFY(indexes.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='index'")));
    QSet<QString> names;
    while (indexes.next())
        names.insert(indexes.value(0).toString());

    QVERIFY(names.contains(QStringLiteral("idx_entries_use_count")));
    QVERIFY(names.contains(QStringLiteral("idx_entries_pinned")));
    QVERIFY(names.contains(QStringLiteral("idx_entries_sensitive")));
    QVERIFY(names.contains(QStringLiteral("idx_entry_groups_group")));
    QVERIFY(names.contains(QStringLiteral("idx_saved_searches_name")));

    indexes = QSqlQuery();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

void TestSchema::normalizesLegacyBlobHashesAndDedupes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy-blob.db"));
    const QString connectionName = QStringLiteral("legacy-blob-test");

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
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral(
            "INSERT INTO entries(timestamp_ms, content_type, content_hash, text_data, preview, size_bytes)"
            " VALUES (1000, 0, :hash, 'blob hashed', 'blob hashed', 10)"));
        insert.bindValue(QStringLiteral(":hash"), QByteArrayLiteral("legacyblobhash"));
        QVERIFY(insert.exec());
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    StorageManager storage(path);
    QSqlQuery typeQuery(storage.database());
    QVERIFY(typeQuery.exec(QStringLiteral("SELECT typeof(content_hash) FROM entries")));
    QVERIFY(typeQuery.next());
    QCOMPARE(typeQuery.value(0).toString(), QStringLiteral("text"));

    // The normalized row participates in dedup again.
    ClipboardRecord record;
    record.hash = QByteArrayLiteral("legacyblobhash");
    record.type = ContentType::Text;
    record.textData = QStringLiteral("blob hashed");
    record.preview = record.textData;
    record.timestamp = 5000;
    bool updated = false;
    const qint64 id = storage.insertOrUpdate(record, &updated);
    QVERIFY(updated);
    QCOMPARE(storage.stats().entryCount, qint64(1));
    QCOMPARE(id, qint64(1));
}

void TestSchema::dedupesCaseInsensitiveSavedSearches()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy-searches.db"));
    const QString connectionName = QStringLiteral("legacy-searches-test");

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE saved_searches ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "filter TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO saved_searches(name, filter) VALUES ('Work', '{}')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO saved_searches(name, filter) VALUES ('work', '{}')")));
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    StorageManager storage(path);
    const auto searches = storage.savedSearches();
    QCOMPARE(searches.size(), 1);
    const qint64 id = searches.first().id;

    // Saving with different casing updates the same row instead of adding one.
    FilterSpec filter;
    filter.searchText = QStringLiteral("x");
    QCOMPARE(storage.addSavedSearch(QStringLiteral("WORK"), filter), id);
    QCOMPARE(storage.savedSearches().size(), 1);
    QCOMPARE(storage.savedSearches().first().filter.searchText, QStringLiteral("x"));
}

void TestSchema::quickCheckReportsHealthyDatabase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("healthy.db")));
    QString error;
    QVERIFY(storage.quickCheck(&error));
    QVERIFY(error.isEmpty());

    // A file that is not a database at all fails the check with a reason.
    const QString junkPath = dir.filePath(QStringLiteral("junk.db"));
    QFile junk(junkPath);
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("this file is not a SQLite database, not even close");
    junk.close();
    StorageManager broken(junkPath);
    error.clear();
    QVERIFY(!broken.quickCheck(&error));
    QVERIFY(!error.isEmpty());
}

void TestSchema::rebuildSearchIndexRepairsMissingIndex()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StorageManager storage(dir.filePath(QStringLiteral("repair.db")));
    QSqlDatabase db = storage.database();
    QVERIFY(SearchEngine::isFtsAvailable(db));

    // Simulate a damaged/missing index: drop it and its triggers, then capture
    // an entry while search indexing is unavailable.
    QSqlQuery drop(db);
    QVERIFY(drop.exec(QStringLiteral("DROP TRIGGER IF EXISTS entries_ai")));
    QVERIFY(drop.exec(QStringLiteral("DROP TRIGGER IF EXISTS entries_ad")));
    QVERIFY(drop.exec(QStringLiteral("DROP TRIGGER IF EXISTS entries_au")));
    QVERIFY(drop.exec(QStringLiteral("DROP TABLE IF EXISTS entries_fts")));
    QVERIFY(!SearchEngine::isFtsAvailable(db));

    ClipboardRecord record;
    record.hash = QByteArrayLiteral("repair-me");
    record.type = ContentType::Text;
    record.textData = QStringLiteral("missing index entry");
    record.preview = record.textData;
    record.timestamp = 1000;
    QVERIFY(storage.insertOrUpdate(record) > 0);

    // Repair: the index and triggers come back and cover the entry captured
    // while they were missing.
    QVERIFY(storage.rebuildSearchIndex());
    QVERIFY(SearchEngine::isFtsAvailable(db));
    FilterSpec filter;
    filter.searchText = QStringLiteral("missing");
    const auto hits = storage.fetchPage(filter, {}, 10);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().hash, QByteArrayLiteral("repair-me"));
}

QTEST_GUILESS_MAIN(TestSchema)
#include "tst_schema.moc"
