#include <QtTest>

#include "BookmarkManager.h"
#include "ExportImportManager.h"
#include "SnippetManager.h"
#include "StorageManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

class TestExportImport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void exportImportRoundTrip();
    void mergeKeepsNewerTimestamp();
    void overwriteReplacesEverything();
    void skipDuplicates();
    void pinnedOnlyExport();
    void groupSubtreePreservesHierarchy();
    void roundTripsOcrTagsSnippetsAndSearches();
    void overwriteClearsAllUserData();
    void importBatchesSignalsIntoOneReset();
    void rejectsMalformedImportFiles();
    void olderVersionExportStillImports();
    void reportsExportWriteErrors();
    void writesAndPrunesAutomaticBackups();
    void importsKlipperHistory();
    void exportsReadingFormats();
    void exportsImagesForSelectionScope();
    void exportsImagesForFilterScope();
    void exportsImagesForPinnedAndGroupSubtree();
    void skipsEntriesWithoutStoredBlob();
    void excludesSensitiveImagesByDefault();
    void includesSensitiveImagesWhenOptedIn();
    void neverOverwritesAndUsesCollisionSafeNames();
    void cancelLeavesNoManifest();
    void manifestOmitsPayloadTextUnlessOptedIn();
    void reportsUnwritableImageTarget();
    void exportsImagesAsJpegThroughEncoderHook();
    void reportsEncoderFailuresWithoutManifest();
    void requiresEncoderForJpegFormat();
    void exportJsonReportsProgressToTotal();
    void exportJsonCancelBeforeStartWritesNothing();
    void exportJsonCancelMidRunWritesNothing();
    void importCancelBeforeStartImportsNothing();
    void importCancelMidRunRollsBack();
    void importReportsProgressToTotal();
    void klipperCancelBeforeStartImportsNothing();
    void backupCancelBeforeStartWritesNothing();

private:
    void seed(StorageManager *storage, BookmarkManager *bookmarks);
    // One stored image; returns the row id. The blob is opaque bytes — the
    // exporter writes them verbatim, so fixtures need no real PNG codec.
    qint64 seedImage(StorageManager *storage, const QByteArray &hash, const QByteArray &blob,
                     qint64 timestamp, const QString &app = QStringLiteral("camera"),
                     bool pinned = false, bool sensitive = false,
                     const QString &text = QString());
    QJsonObject readImageManifest(const QString &manifestPath);

    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
    BookmarkManager *m_bookmarks = nullptr;
    SnippetManager *m_snippets = nullptr;
    ExportImportManager *m_io = nullptr;
};

void TestExportImport::initTestCase()
{
}

void TestExportImport::init()
{
    delete m_io;
    delete m_bookmarks;
    delete m_snippets;
    delete m_storage; // deleteLater() never runs without an event loop
    m_storage = new StorageManager(m_dir.filePath(
        QStringLiteral("history-%1.db").arg(QRandomGenerator::global()->generate64())));
    m_bookmarks = new BookmarkManager(m_storage->database(), this);
    m_snippets = new SnippetManager(m_storage->database(), this);
    m_io = new ExportImportManager(m_storage, m_bookmarks, m_snippets, this);
}

void TestExportImport::seed(StorageManager *storage, BookmarkManager *bookmarks)
{
    for (int i = 0; i < 3; ++i) {
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("hash-") + QByteArray::number(i);
        record.type = ContentType::Text;
        record.textData = QStringLiteral("payload %1").arg(i);
        record.preview = record.textData;
        record.timestamp = 1000 + i;
        record.sizeBytes = record.textData.size();
        record.sourceApp = QStringLiteral("seeder");
        storage->insertOrUpdate(record);
    }
    const qint64 group = bookmarks->createGroup(QStringLiteral("Imported"), 0,
                                                QStringLiteral("#3daee9"), QStringLiteral("folder"));
    bookmarks->assignEntry(1, group);
}

void TestExportImport::exportImportRoundTrip()
{
    seed(m_storage, m_bookmarks);

    const QString path = m_dir.filePath(QStringLiteral("export.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = path;
    QString error;
    QVERIFY2(m_io->exportToFile(request, &error), qPrintable(error));
    QVERIFY(QFile::exists(path));

    // Fresh database, then import.
    init();
    QCOMPARE(m_storage->stats().entryCount, qint64(0));
    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 3);
    QCOMPARE(result.groupsImported, 1);
    QCOMPARE(m_storage->stats().entryCount, qint64(3));

    // Entries are exported newest-first; look the row up by hash, not by id.
    ClipboardRecord full;
    const auto rows = m_storage->fetchAll(FilterSpec{});
    for (const ClipboardRecord &row : rows) {
        if (row.hash == QByteArrayLiteral("hash-0")) {
            QVERIFY(m_storage->fetchFull(row.id, &full));
            break;
        }
    }
    QCOMPARE(full.textData, QStringLiteral("payload 0"));
    QCOMPARE(m_storage->sourceApps(), QStringList{QStringLiteral("seeder")});

    // The imported group exists with the same name and membership.
    const auto groups = m_bookmarks->groups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().name, QStringLiteral("Imported"));
    QCOMPARE(m_bookmarks->entryCount(groups.first().id), 1);
}

void TestExportImport::mergeKeepsNewerTimestamp()
{
    seed(m_storage, m_bookmarks);

    const QString path = m_dir.filePath(QStringLiteral("export2.json"));
    ExportImportManager::ExportRequest request;
    request.path = path;
    request.scope = ExportImportManager::Scope::Everything;
    QString error;
    QVERIFY(m_io->exportToFile(request, &error));

    // Make the local copy of entry #0 newer.
    m_storage->clearHistory(true);
    for (int i = 0; i < 3; ++i) {
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("hash-") + QByteArray::number(i);
        record.type = ContentType::Text;
        record.textData = QStringLiteral("payload %1").arg(i);
        record.preview = record.textData;
        record.timestamp = i == 0 ? 99999 : 1000 + i;
        record.sizeBytes = record.textData.size();
        m_storage->insertOrUpdate(record);
    }

    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesMerged, 3);
    QCOMPARE(m_storage->stats().entryCount, qint64(3)); // no duplicates

    // Look the row up by hash: ids are not stable across clears (AUTOINCREMENT).
    ClipboardRecord full;
    const auto rows = m_storage->fetchAll(FilterSpec{});
    for (const ClipboardRecord &row : rows) {
        if (row.hash == QByteArrayLiteral("hash-0")) {
            QVERIFY(m_storage->fetchFull(row.id, &full));
            break;
        }
    }
    QCOMPARE(full.timestamp, qint64(99999)); // local newer copy wins
}
void TestExportImport::overwriteReplacesEverything()
{
    seed(m_storage, m_bookmarks);

    // Export the 3 seeded entries first...
    const QString path = m_dir.filePath(QStringLiteral("export3.json"));
    ExportImportManager::ExportRequest request;
    request.path = path;
    request.scope = ExportImportManager::Scope::Everything;
    QString error;
    QVERIFY(m_io->exportToFile(request, &error));

    // ...then add a local-only entry that Overwrite must wipe.
    ClipboardRecord extra;
    extra.hash = QByteArrayLiteral("local-only");
    extra.type = ContentType::Text;
    extra.textData = QStringLiteral("local");
    extra.preview = extra.textData;
    extra.timestamp = 42;
    m_storage->insertOrUpdate(extra);
    QCOMPARE(m_storage->stats().entryCount, qint64(4));

    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Overwrite);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(m_storage->stats().entryCount, qint64(3)); // local-only entry gone
}

void TestExportImport::skipDuplicates()
{
    seed(m_storage, m_bookmarks);
    const QString path = m_dir.filePath(QStringLiteral("skip.json"));
    ExportImportManager::ExportRequest request;
    request.path = path;
    QVERIFY(m_io->exportToFile(request));

    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::SkipDuplicates);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 0);
    QCOMPARE(result.entriesSkipped, 3);
    QCOMPARE(m_storage->stats().entryCount, qint64(3));
}

void TestExportImport::pinnedOnlyExport()
{
    seed(m_storage, m_bookmarks);
    ClipboardRecord first;
    const auto rows = m_storage->fetchAll(FilterSpec{});
    QVERIFY(!rows.isEmpty());
    QVERIFY(m_storage->fetchFull(rows.last().id, &first));
    QVERIFY(m_storage->setPinned(first.id, true));

    const QString path = m_dir.filePath(QStringLiteral("pinned.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::PinnedOnly;
    request.path = path;
    QVERIFY(m_io->exportToFile(request));

    init();
    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 1);
    QCOMPARE(m_storage->stats().entryCount, qint64(1));

    ClipboardRecord imported;
    const auto importedRows = m_storage->fetchAll(FilterSpec{});
    QVERIFY(m_storage->fetchFull(importedRows.first().id, &imported));
    QVERIFY(imported.pinned);
}

void TestExportImport::groupSubtreePreservesHierarchy()
{
    ClipboardRecord record;
    record.hash = QByteArrayLiteral("subtree-entry");
    record.type = ContentType::Text;
    record.textData = QStringLiteral("subtree payload");
    record.preview = record.textData;
    record.timestamp = 1000;
    const qint64 entryId = m_storage->insertOrUpdate(record);

    // Names intentionally sort child before parent to exercise import ordering.
    const qint64 root = m_bookmarks->createGroup(QStringLiteral("Zebra"));
    const qint64 child = m_bookmarks->createGroup(QStringLiteral("Alpha"), root);
    const qint64 outside = m_bookmarks->createGroup(QStringLiteral("Outside"));
    QVERIFY(m_bookmarks->assignEntry(entryId, child));
    QVERIFY(outside > 0);

    const QString path = m_dir.filePath(QStringLiteral("subtree.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::GroupSubtree;
    request.groupId = root;
    request.path = path;
    QVERIFY(m_io->exportToFile(request));

    init();
    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 1);

    const auto groups = m_bookmarks->groups();
    QCOMPARE(groups.size(), 2);
    std::optional<BookmarkGroup> importedRoot;
    std::optional<BookmarkGroup> importedChild;
    for (const BookmarkGroup &group : groups) {
        if (group.name == QStringLiteral("Zebra"))
            importedRoot = group;
        if (group.name == QStringLiteral("Alpha"))
            importedChild = group;
    }
    QVERIFY(importedRoot.has_value());
    QVERIFY(importedChild.has_value());
    QCOMPARE(importedRoot->parentId, qint64(0));
    QCOMPARE(importedChild->parentId, importedRoot->id);
    QCOMPARE(m_bookmarks->entryCount(importedChild->id), 1);
}

void TestExportImport::roundTripsOcrTagsSnippetsAndSearches()
{
    ClipboardRecord image;
    image.hash = QByteArrayLiteral("rt-image");
    image.type = ContentType::Image;
    image.blobData = QByteArrayLiteral("PNGDATA");
    image.hasBlob = true;
    image.preview = QStringLiteral("Image 4x4");
    image.timestamp = 1000;
    const qint64 imageId = m_storage->insertOrUpdate(image);
    QVERIFY(imageId > 0);
    QVERIFY(m_storage->setOcrText(imageId, QStringLiteral("recognized words")));
    QVERIFY(m_storage->addTag(imageId, QStringLiteral("work")));
    QVERIFY(m_storage->addTag(imageId, QStringLiteral("screenshots")));

    QVERIFY(m_snippets->createSnippet(QStringLiteral("Signature"),
                                      QStringLiteral("Best, {{clipboard}}"),
                                      QStringLiteral("Meta+Shift+1")) > 0);

    FilterSpec saved;
    saved.searchText = QStringLiteral("invoice");
    saved.sourceApp = QStringLiteral("kate");
    QVERIFY(m_storage->addSavedSearch(QStringLiteral("Invoices"), saved) > 0);

    const QString path = m_dir.filePath(QStringLiteral("v2.json"));
    ExportImportManager::ExportRequest request;
    request.path = path;
    request.scope = ExportImportManager::Scope::Everything;
    QString error;
    QVERIFY2(m_io->exportToFile(request, &error), qPrintable(error));

    init();
    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 1);
    QCOMPARE(result.tagsImported, 2);
    QCOMPARE(result.snippetsImported, 1);
    QCOMPARE(result.savedSearchesImported, 1);

    const auto rows = m_storage->fetchAll(FilterSpec{});
    QCOMPARE(rows.size(), 1);
    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(rows.first().id, &full));
    QCOMPARE(full.ocrText, QStringLiteral("recognized words"));
    QCOMPARE(m_storage->tagsForEntry(rows.first().id),
             (QStringList{QStringLiteral("screenshots"), QStringLiteral("work")}));

    const auto snippets = m_snippets->snippets();
    QCOMPARE(snippets.size(), 1);
    QCOMPARE(snippets.first().name, QStringLiteral("Signature"));
    QCOMPARE(snippets.first().templateText, QStringLiteral("Best, {{clipboard}}"));
    QCOMPARE(snippets.first().shortcut, QStringLiteral("Meta+Shift+1"));

    const auto searches = m_storage->savedSearches();
    QCOMPARE(searches.size(), 1);
    QCOMPARE(searches.first().name, QStringLiteral("Invoices"));
    QCOMPARE(searches.first().filter.searchText, QStringLiteral("invoice"));
    QCOMPARE(searches.first().filter.sourceApp, QStringLiteral("kate"));
}

void TestExportImport::overwriteClearsAllUserData()
{
    seed(m_storage, m_bookmarks);

    const QString path = m_dir.filePath(QStringLiteral("overwrite-all.json"));
    ExportImportManager::ExportRequest request;
    request.path = path;
    QVERIFY(m_io->exportToFile(request));

    // Local-only data of every kind must not survive an Overwrite import.
    ClipboardRecord extra;
    extra.hash = QByteArrayLiteral("local-only");
    extra.type = ContentType::Text;
    extra.textData = QStringLiteral("local");
    extra.preview = extra.textData;
    QVERIFY(m_storage->insertOrUpdate(extra) > 0);
    const auto localRows = m_storage->fetchAll(FilterSpec{});
    QVERIFY(m_storage->addTag(localRows.first().id, QStringLiteral("local-tag")));
    QVERIFY(m_snippets->createSnippet(QStringLiteral("Local snippet"),
                                      QStringLiteral("local template")) > 0);
    QVERIFY(m_storage->addSavedSearch(QStringLiteral("Local search"), FilterSpec{}) > 0);

    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Overwrite);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(m_storage->stats().entryCount, qint64(3));
    QCOMPARE(m_storage->allTags(), QStringList());
    QVERIFY(m_snippets->snippets().isEmpty());
    QVERIFY(m_storage->savedSearches().isEmpty());
}

void TestExportImport::importBatchesSignalsIntoOneReset()
{
    seed(m_storage, m_bookmarks);

    const QString path = m_dir.filePath(QStringLiteral("batched.json"));
    ExportImportManager::ExportRequest request;
    request.path = path;
    QVERIFY(m_io->exportToFile(request));

    init();
    QSignalSpy addedSpy(m_storage, &StorageManager::entryAdded);
    QSignalSpy touchedSpy(m_storage, &StorageManager::entryTouched);
    QSignalSpy resetSpy(m_storage, &StorageManager::storageReset);

    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 3);

    // Bulk mode: the whole import is one transaction and one refresh instead
    // of a signal storm per row.
    QCOMPARE(addedSpy.count(), 0);
    QCOMPARE(touchedSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(m_storage->stats().entryCount, qint64(3));
    QCOMPARE(m_storage->savedSearches().size(), 0); // nothing pending from the bulk
}

void TestExportImport::exportsReadingFormats()
{
    // Delimiters, quotes and line breaks must survive every format.
    ClipboardRecord tricky;
    tricky.hash = QByteArrayLiteral("tricky");
    tricky.type = ContentType::Text;
    tricky.textData = QStringLiteral("line one\nline two, with \"quotes\"");
    tricky.preview = QStringLiteral("line one …");
    tricky.timestamp = 1000;
    tricky.sourceApp = QStringLiteral("kate");
    tricky.pinned = true;
    const qint64 trickyId = m_storage->insertOrUpdate(tricky);
    QVERIFY(trickyId > 0);
    QVERIFY(m_storage->addTag(trickyId, QStringLiteral("work")));

    ClipboardRecord markup;
    markup.hash = QByteArrayLiteral("markup");
    markup.type = ContentType::Text;
    markup.textData = QStringLiteral("<b>bold</b> & \"quoted\"");
    markup.preview = markup.textData;
    markup.timestamp = 2000;
    m_storage->insertOrUpdate(markup);

    ClipboardRecord fenced;
    fenced.hash = QByteArrayLiteral("fenced");
    fenced.type = ContentType::Text;
    fenced.textData = QStringLiteral("a ``` fenced block");
    fenced.preview = fenced.textData;
    fenced.timestamp = 3000;
    m_storage->insertOrUpdate(fenced);

    const auto exportAs = [this](ExportImportManager::ExportFormat format, const QString &name) {
        ExportImportManager::ExportRequest request;
        request.path = m_dir.filePath(name);
        request.format = format;
        QString error;
        if (!m_io->exportToFile(request, &error)) {
            qWarning("export failed: %s", qPrintable(error));
            return QString();
        }
        QFile file(request.path);
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromUtf8(file.readAll());
    };

    // --- CSV: RFC 4180 quoting, tags joined with "; ", flags as 0/1 ----------
    const QString csv = exportAs(ExportImportManager::ExportFormat::Csv, QStringLiteral("h.csv"));
    QVERIFY(!csv.isEmpty());
    QVERIFY(csv.startsWith(QStringLiteral(
        "timestamp,type,source_app,source_window,pinned,sensitive,use_count,tags,text\n")));
    QVERIFY(csv.contains(QStringLiteral(",\"line one\nline two, with \"\"quotes\"\"\"\n")));
    QVERIFY(csv.contains(QStringLiteral(",work,")));
    QVERIFY(csv.contains(QStringLiteral(",\"<b>bold</b> & \"\"quoted\"\"\"\n")));
    QVERIFY(csv.contains(QStringLiteral(",kate,")));
    QVERIFY(csv.contains(QStringLiteral("a ``` fenced block")));

    // --- Markdown: heading per entry, fenced text, longer fence on demand ----
    const QString md = exportAs(ExportImportManager::ExportFormat::Markdown, QStringLiteral("h.md"));
    QVERIFY(!md.isEmpty());
    QVERIFY(md.startsWith(QStringLiteral("# Egoboard history")));
    QVERIFY(md.contains(QStringLiteral("· pinned")));
    QVERIFY(md.contains(QStringLiteral("Tags: work")));
    QVERIFY(md.contains(QStringLiteral("```\nline one\nline two, with \"quotes\"\n```")));
    QVERIFY(md.contains(QStringLiteral("````\na ``` fenced block\n````")));

    // --- HTML: escaped text, table rows ---------------------------------------
    const QString html = exportAs(ExportImportManager::ExportFormat::Html, QStringLiteral("h.html"));
    QVERIFY(!html.isEmpty());
    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(html.contains(QStringLiteral("&lt;b&gt;bold&lt;/b&gt; &amp; &quot;quoted&quot;")));
    QVERIFY(!html.contains(QStringLiteral("<b>bold</b>")));
    QVERIFY(html.contains(QStringLiteral("<td>kate (pinned)</td>")));
    QVERIFY(html.contains(QStringLiteral("<td>work</td>")));
    QVERIFY(html.endsWith(QStringLiteral("</html>\n")));

    // Every entry appears exactly once in each format.
    QCOMPARE(csv.count(QStringLiteral("plain entry")), 0); // none in this fixture
    QCOMPARE(csv.count(QStringLiteral("line one")), 1);
    QCOMPARE(md.count(QStringLiteral("## ")), 3);
    QCOMPARE(html.count(QStringLiteral("<tr><td>")), 3); // header row has <th> cells
}

void TestExportImport::writesAndPrunesAutomaticBackups()
{
    seed(m_storage, m_bookmarks);
    const QString folder = m_dir.filePath(QStringLiteral("backups"));

    // Keeps everything when prune is disabled, and creates the folder itself.
    QStringList written;
    for (int i = 0; i < 3; ++i) {
        const auto result = m_io->writeBackup(folder, 0);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(QFile::exists(result.path));
        QVERIFY(result.path.contains(QStringLiteral("egoboard-backup-")));
        QCOMPARE(result.pruned, 0);
        written << result.path;
    }
    const QStringList names = ExportImportManager::listBackups(folder);
    QCOMPARE(names.size(), 3);
    QCOMPARE(names.first(), written.last()); // newest first

    // Pruning keeps the newest N and reports how many files it removed.
    QCOMPARE(ExportImportManager::pruneBackups(folder, 1), 2);
    const QStringList remaining = ExportImportManager::listBackups(folder);
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first(), written.last());
    QVERIFY(QFile::exists(remaining.first()));

    // The written file is a real export: importing it back into a fresh
    // database restores the entries.
    init();
    const auto imported = m_io->importFromFile(remaining.first(),
                                               ExportImportManager::ImportMode::Merge);
    QVERIFY2(imported.ok, qPrintable(imported.error));
    QCOMPARE(imported.entriesImported, 3);

    // An unwritable/empty configuration is reported instead of crashing.
    const auto noFolder = m_io->writeBackup(QString(), 3);
    QVERIFY(!noFolder.ok);
    QVERIFY(!noFolder.error.isEmpty());
}

void TestExportImport::importsKlipperHistory()
{
    // Build a database shaped like Klipper's history3.sqlite (KF6 schema).
    const QString klipperPath = m_dir.filePath(QStringLiteral("history3.sqlite"));
    const QString connectionName = QStringLiteral("klipper-fixture");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(klipperPath);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE main (uuid char(40) PRIMARY KEY, added_time REAL NOT NULL,"
            " last_used_time REAL, mimetypes TEXT NOT NULL, text NTEXT, starred BOOLEAN)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE version (db_version INT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO version (db_version) VALUES (3)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO main (uuid, added_time, last_used_time, mimetypes, text, starred)"
            " VALUES ('a', 1500000000.5, 1500000100.0, 'text/plain', 'first klipper entry', 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO main (uuid, added_time, last_used_time, mimetypes, text, starred)"
            " VALUES ('b', 1500000500.0, 1500000600.0, 'text/plain', 'starred klipper entry', 1)")));
        // Image items have no text and are skipped.
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO main (uuid, added_time, last_used_time, mimetypes, text, starred)"
            " VALUES ('c', 1500000700.0, 1500000700.0, 'image/png', NULL, 0)")));
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    // An entry that also exists in the local history must merge, not duplicate.
    // The importer hashes tag + NUL + payload (same as live captures do).
    ClipboardRecord existing;
    existing.type = ContentType::Text;
    existing.textData = QStringLiteral("first klipper entry");
    existing.preview = existing.textData;
    existing.timestamp = 1000;
    existing.hash = QCryptographicHash::hash(QByteArrayLiteral("text\0first klipper entry"),
                                             QCryptographicHash::Sha256)
                        .toHex();
    QVERIFY(m_storage->insertOrUpdate(existing) > 0);

    const auto result = m_io->importKlipperHistory(klipperPath);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 1); // the starred one
    QCOMPARE(result.entriesMerged, 1); // the one already present
    QCOMPARE(result.entriesSkipped, 1); // the imageless row

    const auto rows = m_storage->fetchAll(FilterSpec{});
    QCOMPARE(rows.size(), 2);

    // Starred becomes pinned, Klipper's copy time is preserved (seconds → ms).
    FilterSpec pinned;
    pinned.pinnedOnly = true;
    const auto pinnedRows = m_storage->fetchAll(pinned);
    QCOMPARE(pinnedRows.size(), 1);
    QCOMPARE(pinnedRows.first().preview, QStringLiteral("starred klipper entry"));
    QCOMPARE(pinnedRows.first().timestamp, qint64(1500000500) * 1000);
    QCOMPARE(pinnedRows.first().sourceApp, QStringLiteral("klipper"));

    // Importing again changes nothing (content-hash dedup) and reports merges.
    const auto again = m_io->importKlipperHistory(klipperPath);
    QVERIFY2(again.ok, qPrintable(again.error));
    QCOMPARE(again.entriesImported, 0);
    QCOMPARE(m_storage->fetchAll(FilterSpec{}).size(), 2);

    // A file that is not a Klipper database is rejected with a reason.
    const QString junk = m_dir.filePath(QStringLiteral("not-klipper.sqlite"));
    {
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a database");
    }
    const auto bad = m_io->importKlipperHistory(junk);
    QVERIFY(!bad.ok);
    QVERIFY(!bad.error.isEmpty());

    const auto missing = m_io->importKlipperHistory(m_dir.filePath(QStringLiteral("absent.sqlite")));
    QVERIFY(!missing.ok);
    QVERIFY(!missing.error.isEmpty());
}

void TestExportImport::rejectsMalformedImportFiles()
{
    const QString malformed = m_dir.filePath(QStringLiteral("malformed.json"));
    {
        QFile file(malformed);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write("not json") > 0);
    }
    auto result = m_io->importFromFile(malformed, ExportImportManager::ImportMode::Merge);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(m_storage->stats().entryCount, qint64(0));

    const QString wrongFormat = m_dir.filePath(QStringLiteral("wrong-format.json"));
    {
        QFile file(wrongFormat);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(R"({"format":"other","version":1})") > 0);
    }
    result = m_io->importFromFile(wrongFormat, ExportImportManager::ImportMode::Merge);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());

    const QString newer = m_dir.filePath(QStringLiteral("newer.json"));
    {
        QFile file(newer);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(R"({"format":"egoboard-export","version":3})") > 0);
    }
    result = m_io->importFromFile(newer, ExportImportManager::ImportMode::Merge);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("newer"), Qt::CaseInsensitive));
}

void TestExportImport::olderVersionExportStillImports()
{
    // P1 data safety: a version-1 export (sparse keys, no newer fields)
    // imports cleanly into the current schema — old backups keep working.
    const QString path = m_dir.filePath(QStringLiteral("v1-export.json"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(R"({"format":"egoboard-export","version":1,)"
                           R"("groups":[{"id":7,"name":"OldGroup"}],)"
                           R"("entries":[{"hash":"v1hash","timestamp":4242,)"
                           R"("type":"text","text":"legacy payload"}]})")
                > 0);
    }
    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.entriesImported, 1);
    QCOMPARE(result.groupsImported, 1);
    QCOMPARE(m_storage->stats().entryCount, qint64(1));

    // Content survives the sparse round-trip; absent keys read as defaults.
    // (fetchPage rows are light — no text payload — so re-read the full row.)
    const auto page = m_storage->fetchPage({}, {}, 10);
    QCOMPARE(page.size(), 1);
    QCOMPARE(page.first().timestamp, qint64(4242));
    ClipboardRecord full;
    QVERIFY(m_storage->fetchFull(page.first().id, &full));
    QCOMPARE(full.textData, QStringLiteral("legacy payload"));
    QVERIFY(!full.pinned);
    const auto groups = m_bookmarks->groups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().name, QStringLiteral("OldGroup"));
}

void TestExportImport::reportsExportWriteErrors()
{
    ExportImportManager::ExportRequest request;
    request.path = m_dir.filePath(QStringLiteral("missing-directory/export.json"));
    QString error;
    QVERIFY(!m_io->exportToFile(request, &error));
    QVERIFY(!error.isEmpty());
}

qint64 TestExportImport::seedImage(StorageManager *storage, const QByteArray &hash,
                                   const QByteArray &blob, qint64 timestamp, const QString &app,
                                   bool pinned, bool sensitive, const QString &text)
{
    ClipboardRecord record;
    record.hash = hash;
    record.type = ContentType::Image;
    record.blobData = blob;
    record.hasBlob = !blob.isEmpty();
    record.textData = text;
    record.preview = QStringLiteral("image");
    record.timestamp = timestamp;
    record.sizeBytes = blob.size();
    record.sourceApp = app;
    record.pinned = pinned;
    record.sensitive = sensitive;
    const qint64 id = storage->insertOrUpdate(record);
    if (id == 0)
        qWarning("seedImage: insert failed for hash %s", hash.constData());
    return id;
}

QJsonObject TestExportImport::readImageManifest(const QString &manifestPath)
{
    QFile file(manifestPath);
    if (manifestPath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        qWarning("readImageManifest: cannot open %s", qPrintable(manifestPath));
        return {};
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("readImageManifest: invalid JSON in %s", qPrintable(manifestPath));
        return {};
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("egoboard-image-export")
        || root.value(QStringLiteral("version")).toInt() != 1) {
        qWarning("readImageManifest: unexpected schema in %s", qPrintable(manifestPath));
        return {};
    }
    return root;
}

void TestExportImport::exportsImagesForSelectionScope()
{
    const qint64 first = seedImage(m_storage, QByteArrayLiteral("img-a"),
                                   QByteArrayLiteral("PNG-DATA-A"), 1700000000000);
    const qint64 second = seedImage(m_storage, QByteArrayLiteral("img-b"),
                                    QByteArrayLiteral("PNG-DATA-B"), 1700000001000);
    seed(m_storage, m_bookmarks); // 3 text entries: skipped as non-images

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Selection;
    request.entryIds = {first, second, first}; // duplicates collapse to one file each
    request.dir = m_dir.filePath(QStringLiteral("images-selection"));
    int progressCalls = 0;
    int lastExported = -1;
    const auto result = m_io->exportImages(
        request, nullptr,
        [&](int exported, int skipped, qint64 bytes) {
            ++progressCalls;
            lastExported = exported;
            Q_UNUSED(skipped);
            Q_UNUSED(bytes);
        });
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.canceled);
    QCOMPARE(result.exported, 2);
    QCOMPARE(result.skippedNonImage, 0); // selection listed images only
    QCOMPARE(result.skippedNoBlob, 0);
    QCOMPARE(result.skippedSensitive, 0);
    QCOMPARE(result.bytesWritten, qint64(QByteArrayLiteral("PNG-DATA-A").size()
                                         + QByteArrayLiteral("PNG-DATA-B").size()));
    QCOMPARE(result.files.size(), 2);
    QVERIFY(progressCalls > 0);
    QCOMPARE(lastExported, 2);
    for (const QString &path : result.files) {
        QVERIFY(path.endsWith(QStringLiteral(".png")));
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QByteArray payload = file.readAll();
        QVERIFY(payload == QByteArrayLiteral("PNG-DATA-A")
                || payload == QByteArrayLiteral("PNG-DATA-B"));
    }

    const QJsonObject root = readImageManifest(result.manifestPath);
    QCOMPARE(root.value(QStringLiteral("scope")).toString(), QStringLiteral("selection"));
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 2);
    QSet<qint64> manifestIds;
    for (const auto &value : files) {
        const QJsonObject entry = value.toObject();
        QVERIFY(!entry.value(QStringLiteral("file")).toString().isEmpty());
        manifestIds.insert(qint64(entry.value(QStringLiteral("id")).toDouble()));
        QCOMPARE(entry.value(QStringLiteral("sourceApp")).toString(), QStringLiteral("camera"));
        // Payload text stays out of the manifest unless explicitly requested.
        QVERIFY(!entry.contains(QStringLiteral("text")));
    }
    QVERIFY(manifestIds.contains(first));
    QVERIFY(manifestIds.contains(second));
    // The history itself is untouched by the export.
    QCOMPARE(m_storage->stats().entryCount, qint64(5));
}

void TestExportImport::exportsImagesForFilterScope()
{
    seedImage(m_storage, QByteArrayLiteral("img-cam"), QByteArrayLiteral("PNG-CAM"), 1700000000000,
              QStringLiteral("camera"));
    seedImage(m_storage, QByteArrayLiteral("img-shot"), QByteArrayLiteral("PNG-SHOT"),
              1700000001000, QStringLiteral("screenshot-tool"));
    seed(m_storage, m_bookmarks); // text entries from "seeder"

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::CurrentFilter;
    request.filter.sourceApp = QStringLiteral("camera");
    request.filter.contentType = int(ContentType::Image);
    request.dir = m_dir.filePath(QStringLiteral("images-filter"));
    const auto result = m_io->exportImages(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.exported, 1);
    QCOMPARE(result.files.size(), 1);
    const QJsonObject root = readImageManifest(result.manifestPath);
    QCOMPARE(root.value(QStringLiteral("scope")).toString(), QStringLiteral("filter"));
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.first().toObject().value(QStringLiteral("sourceApp")).toString(),
             QStringLiteral("camera"));
}

void TestExportImport::exportsImagesForPinnedAndGroupSubtree()
{
    const qint64 pinned = seedImage(m_storage, QByteArrayLiteral("img-pin"),
                                    QByteArrayLiteral("PNG-PIN"), 1700000000000,
                                    QStringLiteral("camera"), true);
    const qint64 plain = seedImage(m_storage, QByteArrayLiteral("img-plain"),
                                   QByteArrayLiteral("PNG-PLAIN"), 1700000001000);
    const qint64 group = m_bookmarks->createGroup(QStringLiteral("Shots"), 0,
                                                  QStringLiteral("#3daee9"), QStringLiteral("folder"));
    QVERIFY(group != 0);
    QVERIFY(m_bookmarks->assignEntry(pinned, group));
    QVERIFY(m_bookmarks->assignEntry(plain, group));

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::PinnedOnly;
    request.dir = m_dir.filePath(QStringLiteral("images-pinned"));
    const auto pinnedResult = m_io->exportImages(request);
    QVERIFY2(pinnedResult.ok, qPrintable(pinnedResult.error));
    QCOMPARE(pinnedResult.exported, 1);
    QCOMPARE(pinnedResult.files.size(), 1);

    request.scope = ExportImportManager::ImageExportRequest::Scope::GroupSubtree;
    request.groupId = group;
    request.dir = m_dir.filePath(QStringLiteral("images-group"));
    const auto groupResult = m_io->exportImages(request);
    QVERIFY2(groupResult.ok, qPrintable(groupResult.error));
    QCOMPARE(groupResult.exported, 2);
    const QJsonObject root = readImageManifest(groupResult.manifestPath);
    QCOMPARE(root.value(QStringLiteral("scope")).toString(), QStringLiteral("group"));
    QCOMPARE(root.value(QStringLiteral("files")).toArray().size(), 2);
}

void TestExportImport::skipsEntriesWithoutStoredBlob()
{
    seedImage(m_storage, QByteArrayLiteral("img-blob"), QByteArrayLiteral("PNG-REAL"),
              1700000000000);
    // Image entry whose blob never arrived (e.g. capped capture): reported as
    // skipped instead of producing an empty file.
    seedImage(m_storage, QByteArrayLiteral("img-empty"), QByteArray(), 1700000001000);
    ClipboardRecord text;
    text.hash = QByteArrayLiteral("txt-1");
    text.type = ContentType::Text;
    text.textData = QStringLiteral("not an image");
    text.preview = text.textData;
    text.timestamp = 1700000002000;
    QVERIFY(m_storage->insertOrUpdate(text) != 0);

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.dir = m_dir.filePath(QStringLiteral("images-skipped"));
    const auto result = m_io->exportImages(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.exported, 1);
    QCOMPARE(result.skippedNoBlob, 1);
    QCOMPARE(result.skippedNonImage, 1);
    QCOMPARE(result.files.size(), 1);
    const QDir dir(request.dir);
    QCOMPARE(dir.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files).size(), 1);
}

void TestExportImport::excludesSensitiveImagesByDefault()
{
    seedImage(m_storage, QByteArrayLiteral("img-safe"), QByteArrayLiteral("PNG-SAFE"),
              1700000000000);
    seedImage(m_storage, QByteArrayLiteral("img-secret"), QByteArrayLiteral("PNG-SECRET"),
              1700000001000, QStringLiteral("camera"), false, true);

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.dir = m_dir.filePath(QStringLiteral("images-nosensitive"));
    const auto result = m_io->exportImages(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.exported, 1);
    QCOMPARE(result.skippedSensitive, 1);
    const QJsonObject root = readImageManifest(result.manifestPath);
    QCOMPARE(root.value(QStringLiteral("includeSensitive")).toBool(), false);
    QCOMPARE(root.value(QStringLiteral("files")).toArray().size(), 1);
}

void TestExportImport::includesSensitiveImagesWhenOptedIn()
{
    seedImage(m_storage, QByteArrayLiteral("img-safe"), QByteArrayLiteral("PNG-SAFE"),
              1700000000000);
    seedImage(m_storage, QByteArrayLiteral("img-secret"), QByteArrayLiteral("PNG-SECRET"),
              1700000001000, QStringLiteral("camera"), false, true);
    QVERIFY(m_storage->addTag(2, QStringLiteral("private")));
    QVERIFY(m_storage->setOcrText(2, QStringLiteral("recognized secret")));

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.includeSensitive = true;
    request.dir = m_dir.filePath(QStringLiteral("images-sensitive"));
    const auto result = m_io->exportImages(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.exported, 2);
    QCOMPARE(result.skippedSensitive, 0);
    const QJsonObject root = readImageManifest(result.manifestPath);
    QCOMPARE(root.value(QStringLiteral("includeSensitive")).toBool(), true);
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 2);
    // Metadata (tags, OCR) travels with the manifest entry.
    bool sawTags = false;
    bool sawOcr = false;
    for (const auto &value : files) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toDouble() == 2.0) {
            sawTags = entry.value(QStringLiteral("tags")).toArray().size() == 1;
            sawOcr = entry.value(QStringLiteral("ocrText")).toString()
                == QStringLiteral("recognized secret");
        }
    }
    QVERIFY(sawTags);
    QVERIFY(sawOcr);
}

void TestExportImport::neverOverwritesAndUsesCollisionSafeNames()
{
    // Same capture timestamp twice: the row id keeps the names distinct.
    const qint64 first = seedImage(m_storage, QByteArrayLiteral("img-dup-a"),
                                   QByteArrayLiteral("PNG-DUP-A"), 1700000000000);
    const qint64 second = seedImage(m_storage, QByteArrayLiteral("img-dup-b"),
                                    QByteArrayLiteral("PNG-DUP-B"), 1700000000000);
    QVERIFY(first != second);

    // A pre-existing user file at the first image's deterministic name must
    // survive: the exporter takes a `-2` suffix instead of overwriting.
    const QString stamp = QDateTime::fromMSecsSinceEpoch(1700000000000)
                              .toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString clashingName = QStringLiteral("egoboard-%1-%2-%3.png")
                                     .arg(stamp)
                                     .arg(first)
                                     .arg(QString::fromLatin1(QByteArrayLiteral("img-dup-a").left(8)));
    const QString targetDir = m_dir.filePath(QStringLiteral("images-collision"));
    QVERIFY(QDir().mkpath(targetDir));
    {
        QFile clash(targetDir + QLatin1Char('/') + clashingName);
        QVERIFY(clash.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(clash.write("user content") > 0);
    }
    // Same for the manifest: a sentinel manifest.json forces a numbered one.
    {
        QFile sentinel(targetDir + QStringLiteral("/manifest.json"));
        QVERIFY(sentinel.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(sentinel.write("sentinel") > 0);
    }

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.dir = targetDir;
    const auto result = m_io->exportImages(request);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.exported, 2);
    // Both files are distinct and neither is the clashing name.
    QCOMPARE(result.files.size(), 2);
    QVERIFY(result.files.at(0) != result.files.at(1));
    QVERIFY(!result.files.at(0).endsWith(clashingName));
    QVERIFY(!result.files.at(1).endsWith(clashingName));
    bool sawSuffixed = false;
    for (const QString &path : result.files)
        sawSuffixed = sawSuffixed || QFileInfo(path).fileName().contains(QStringLiteral("-2.png"));
    QVERIFY(sawSuffixed);
    // The user's files are byte-identical afterwards.
    {
        QFile clash(targetDir + QLatin1Char('/') + clashingName);
        QVERIFY(clash.open(QIODevice::ReadOnly));
        QCOMPARE(clash.readAll(), QByteArrayLiteral("user content"));
        QFile sentinel(targetDir + QStringLiteral("/manifest.json"));
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.readAll(), QByteArrayLiteral("sentinel"));
    }
    QVERIFY(result.manifestPath.endsWith(QStringLiteral("manifest-2.json")));
    const QJsonObject root = readImageManifest(result.manifestPath);
    QCOMPARE(root.value(QStringLiteral("files")).toArray().size(), 2);
}

void TestExportImport::cancelLeavesNoManifest()
{
    seedImage(m_storage, QByteArrayLiteral("img-a"), QByteArrayLiteral("PNG-A"), 1700000000000);
    seedImage(m_storage, QByteArrayLiteral("img-b"), QByteArrayLiteral("PNG-B"), 1700000001000);

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.dir = m_dir.filePath(QStringLiteral("images-canceled"));
    std::atomic<bool> cancel{true}; // canceled before the first batch
    const auto result = m_io->exportImages(request, &cancel);
    QVERIFY(!result.ok);
    QVERIFY(result.canceled);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.manifestPath.isEmpty());
    QVERIFY(!QFile::exists(request.dir + QStringLiteral("/manifest.json")));
    QCOMPARE(result.exported, 0);
}

void TestExportImport::manifestOmitsPayloadTextUnlessOptedIn()
{
    seedImage(m_storage, QByteArrayLiteral("img-t"), QByteArrayLiteral("PNG-T"), 1700000000000,
              QStringLiteral("camera"), false, false, QStringLiteral("alt text payload"));

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.dir = m_dir.filePath(QStringLiteral("images-notext"));
    const auto plain = m_io->exportImages(request);
    QVERIFY2(plain.ok, qPrintable(plain.error));
    QJsonArray files = readImageManifest(plain.manifestPath).value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 1);
    QVERIFY(!files.first().toObject().contains(QStringLiteral("text")));

    request.includeText = true;
    request.dir = m_dir.filePath(QStringLiteral("images-withtext"));
    const auto withText = m_io->exportImages(request);
    QVERIFY2(withText.ok, qPrintable(withText.error));
    const QJsonObject root = readImageManifest(withText.manifestPath);
    QCOMPARE(root.value(QStringLiteral("includeText")).toBool(), true);
    files = root.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.first().toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("alt text payload"));
}

void TestExportImport::reportsUnwritableImageTarget()
{
    seedImage(m_storage, QByteArrayLiteral("img-a"), QByteArrayLiteral("PNG-A"), 1700000000000);
    // A regular file where the folder should be: mkpath fails deterministically.
    const QString blocker = m_dir.filePath(QStringLiteral("blocker"));
    {
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write("block") > 0);
    }
    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.dir = blocker + QStringLiteral("/subfolder");
    const auto result = m_io->exportImages(request);
    QVERIFY(!result.ok);
    QVERIFY(!result.canceled);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.manifestPath.isEmpty());
}

void TestExportImport::exportsImagesAsJpegThroughEncoderHook()
{
    const qint64 first = seedImage(m_storage, QByteArrayLiteral("img-a"),
                                   QByteArrayLiteral("PNG-DATA-A"), 1700000000000);
    const qint64 second = seedImage(m_storage, QByteArrayLiteral("img-b"),
                                    QByteArrayLiteral("PNG-DATA-B"), 1700000001000);
    // Core stays GUI-free: the caller converts (the app passes a QImage-based
    // encoder); this fake stands in for it and proves the hook contract.
    QSet<qint64> encodedIds;
    ExportImportManager::ImageEncoder fakeJpeg =
        [&](const QByteArray &storedPng, qint64 entryId, QString *extension, QString *) {
            encodedIds.insert(entryId);
            if (extension)
                *extension = QStringLiteral("jpg");
            return QByteArrayLiteral("JPEG:") + storedPng;
        };

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Selection;
    request.entryIds = {first, second};
    request.fileFormat = ExportImportManager::ImageExportRequest::ImageFileFormat::Jpeg;
    request.jpegQuality = 70;
    request.dir = m_dir.filePath(QStringLiteral("images-jpeg"));
    const auto result = m_io->exportImages(request, nullptr, {}, fakeJpeg);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.exported, 2);
    QCOMPARE(result.bytesWritten,
             qint64(QByteArrayLiteral("JPEG:PNG-DATA-A").size()
                    + QByteArrayLiteral("JPEG:PNG-DATA-B").size()));
    QVERIFY(encodedIds.contains(first));
    QVERIFY(encodedIds.contains(second));
    for (const QString &path : result.files) {
        QVERIFY(path.endsWith(QStringLiteral(".jpg")));
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        QVERIFY(file.readAll().startsWith(QByteArrayLiteral("JPEG:")));
    }
    const QJsonObject root = readImageManifest(result.manifestPath);
    QCOMPARE(root.value(QStringLiteral("fileFormat")).toString(), QStringLiteral("jpeg"));
    QCOMPARE(root.value(QStringLiteral("jpegQuality")).toInt(), 70);
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 2);
    for (const auto &value : files)
        QVERIFY(value.toObject().value(QStringLiteral("file")).toString().endsWith(
            QStringLiteral(".jpg")));
}

void TestExportImport::reportsEncoderFailuresWithoutManifest()
{
    seedImage(m_storage, QByteArrayLiteral("img-a"), QByteArrayLiteral("PNG-A"), 1700000000000);
    seedImage(m_storage, QByteArrayLiteral("img-b"), QByteArrayLiteral("PNG-B"), 1700000001000);
    ExportImportManager::ImageEncoder failing =
        [](const QByteArray &, qint64 entryId, QString *, QString *error) {
            if (error)
                *error = QStringLiteral("no decoder for entry %1").arg(entryId);
            return QByteArray();
        };

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.fileFormat = ExportImportManager::ImageExportRequest::ImageFileFormat::Jpeg;
    request.dir = m_dir.filePath(QStringLiteral("images-encode-fail"));
    const auto result = m_io->exportImages(request, nullptr, {}, failing);
    QVERIFY(!result.ok);
    QVERIFY(!result.canceled);
    QVERIFY(result.error.contains(QStringLiteral("no decoder")));
    QVERIFY(result.manifestPath.isEmpty());
    QVERIFY(!QFile::exists(request.dir + QStringLiteral("/manifest.json")));
}

void TestExportImport::requiresEncoderForJpegFormat()
{
    seedImage(m_storage, QByteArrayLiteral("img-a"), QByteArrayLiteral("PNG-A"), 1700000000000);

    ExportImportManager::ImageExportRequest request;
    request.scope = ExportImportManager::ImageExportRequest::Scope::Everything;
    request.fileFormat = ExportImportManager::ImageExportRequest::ImageFileFormat::Jpeg;
    request.dir = m_dir.filePath(QStringLiteral("images-no-encoder"));
    const auto result = m_io->exportImages(request); // no encoder hooked up
    QVERIFY(!result.ok);
    QVERIFY(!result.canceled);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(result.exported, 0);
    QVERIFY(result.manifestPath.isEmpty());
}

void TestExportImport::exportJsonReportsProgressToTotal()
{
    // U11 (G9): progress is monotonic and ends at (total, total).
    seed(m_storage, m_bookmarks);
    QVector<QPair<int, int>> calls;
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = m_dir.filePath(QStringLiteral("progress.json"));
    QString error;
    QVERIFY2(m_io->exportToFile(request, &error, nullptr,
                                [&](int done, int total) { calls.append({done, total}); }),
             qPrintable(error));
    QVERIFY(!calls.isEmpty());
    for (int i = 1; i < calls.size(); ++i) {
        QVERIFY(calls.at(i).first >= calls.at(i - 1).first); // done never goes back
        QVERIFY(calls.at(i).second >= calls.at(i - 1).second); // nor does total
    }
    QCOMPARE(calls.last().first, calls.last().second);
    QVERIFY(calls.last().second >= 3);
}

void TestExportImport::exportJsonCancelBeforeStartWritesNothing()
{
    seed(m_storage, m_bookmarks);
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = m_dir.filePath(QStringLiteral("canceled-before.json"));
    std::atomic<bool> cancel{true}; // canceled before the first byte
    QString error;
    QVERIFY(!m_io->exportToFile(request, &error, &cancel, {}));
    QVERIFY(error.contains(QStringLiteral("canceled")));
    QVERIFY(!QFile::exists(request.path));
}

void TestExportImport::exportJsonCancelMidRunWritesNothing()
{
    // 300 entries span two 200-row gather pages; canceling in the first
    // page's callback aborts before any file is opened.
    for (int i = 0; i < 300; ++i) {
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("cancel-bulk-") + QByteArray::number(i);
        record.type = ContentType::Text;
        record.textData = QStringLiteral("payload %1").arg(i);
        record.preview = record.textData;
        record.timestamp = 1000 + i;
        record.sizeBytes = record.textData.size();
        record.sourceApp = QStringLiteral("seeder");
        QVERIFY(m_storage->insertOrUpdate(record) > 0);
    }
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = m_dir.filePath(QStringLiteral("canceled-mid.json"));
    std::atomic<bool> cancel{false};
    QString error;
    QVERIFY(!m_io->exportToFile(request, &error, &cancel,
                                [&](int done, int) {
                                    if (done >= 64)
                                        cancel.store(true, std::memory_order_relaxed);
                                }));
    QVERIFY(error.contains(QStringLiteral("canceled")));
    QVERIFY(!QFile::exists(request.path)); // gather/serialize abort: no file
}

void TestExportImport::importCancelBeforeStartImportsNothing()
{
    seed(m_storage, m_bookmarks);
    const QString path = m_dir.filePath(QStringLiteral("cancel-import.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = path;
    QString error;
    QVERIFY2(m_io->exportToFile(request, &error), qPrintable(error));

    init(); // fresh database
    QSignalSpy resetSpy(m_storage, &StorageManager::storageReset);
    std::atomic<bool> cancel{true};
    const auto result = m_io->importFromFile(path, ExportImportManager::ImportMode::Merge,
                                             &cancel, {});
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("canceled")));
    QCOMPARE(m_storage->stats().entryCount, qint64(0));
    QCOMPARE(resetSpy.count(), 0); // no reset: nothing changed
}

void TestExportImport::importCancelMidRunRollsBack()
{
    // Overwrite wipes first inside the bulk transaction: canceling mid-run
    // must roll everything back, including the wipe.
    for (int i = 0; i < 300; ++i) {
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("rollback-") + QByteArray::number(i);
        record.type = ContentType::Text;
        record.textData = QStringLiteral("payload %1").arg(i);
        record.preview = record.textData;
        record.timestamp = 1000 + i;
        record.sizeBytes = record.textData.size();
        record.sourceApp = QStringLiteral("seeder");
        QVERIFY(m_storage->insertOrUpdate(record) > 0);
    }
    const QString path = m_dir.filePath(QStringLiteral("rollback.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = path;
    QString error;
    QVERIFY2(m_io->exportToFile(request, &error), qPrintable(error));

    init(); // fresh database with two survivors of its own
    for (const char *name : {"survivor-1", "survivor-2"}) {
        ClipboardRecord record;
        record.hash = QByteArray(name);
        record.type = ContentType::Text;
        record.textData = QString::fromLatin1(name);
        record.preview = record.textData;
        record.timestamp = 5000;
        record.sizeBytes = record.textData.size();
        record.sourceApp = QStringLiteral("seeder");
        QVERIFY(m_storage->insertOrUpdate(record) > 0);
    }
    QSignalSpy resetSpy(m_storage, &StorageManager::storageReset);
    std::atomic<bool> cancel{false};
    const auto result = m_io->importFromFile(
        path, ExportImportManager::ImportMode::Overwrite, &cancel, [&](int done, int) {
            if (done >= 64)
                cancel.store(true, std::memory_order_relaxed);
        });
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("canceled")));
    QCOMPARE(m_storage->stats().entryCount, qint64(2)); // wipe rolled back
    QCOMPARE(resetSpy.count(), 0);
}

void TestExportImport::importReportsProgressToTotal()
{
    seed(m_storage, m_bookmarks);
    const QString path = m_dir.filePath(QStringLiteral("import-progress.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = path;
    QString error;
    QVERIFY2(m_io->exportToFile(request, &error), qPrintable(error));

    init();
    QVector<QPair<int, int>> calls;
    const auto result = m_io->importFromFile(
        path, ExportImportManager::ImportMode::Merge, nullptr,
        [&](int done, int total) { calls.append({done, total}); });
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!calls.isEmpty());
    for (int i = 1; i < calls.size(); ++i)
        QVERIFY(calls.at(i).first >= calls.at(i - 1).first);
    QCOMPARE(calls.last().first, calls.last().second);
    QCOMPARE(calls.last().second, 3);
}

void TestExportImport::klipperCancelBeforeStartImportsNothing()
{
    const QString klipperPath = m_dir.filePath(QStringLiteral("cancel-history3.sqlite"));
    const QString connectionName = QStringLiteral("klipper-cancel-fixture");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(klipperPath);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE main (uuid char(40) PRIMARY KEY, added_time REAL NOT NULL,"
            " last_used_time REAL, mimetypes TEXT NOT NULL, text NTEXT, starred BOOLEAN)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO main (uuid, added_time, last_used_time, mimetypes, text, starred)"
            " VALUES ('k', 1500000000.5, 1500000100.0, 'text/plain', 'klipper canceled', 0)")));
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
    QSignalSpy resetSpy(m_storage, &StorageManager::storageReset);
    std::atomic<bool> cancel{true};
    const auto result = m_io->importKlipperHistory(klipperPath, &cancel, {});
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("canceled")));
    QCOMPARE(m_storage->stats().entryCount, qint64(0));
    QCOMPARE(resetSpy.count(), 0);
}

void TestExportImport::backupCancelBeforeStartWritesNothing()
{
    seed(m_storage, m_bookmarks);
    const QString folder = m_dir.filePath(QStringLiteral("backups-canceled"));
    std::atomic<bool> cancel{true};
    const auto result = m_io->writeBackup(folder, 3, &cancel, {});
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("canceled")));
    QVERIFY(ExportImportManager::listBackups(folder).isEmpty());
}

QTEST_GUILESS_MAIN(TestExportImport)
#include "tst_exportimport.moc"
