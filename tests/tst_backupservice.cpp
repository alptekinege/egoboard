#include <QtTest>

#include "../src/app/BackupService.h"
#include "../src/app/SettingsManager.h"
#include "BookmarkManager.h"
#include "ExportImportManager.h"
#include "SnippetManager.h"
#include "StorageManager.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

// Automatic backups: the service schedules the work, the worker opens its own
// connection on its own thread and the written file must restore cleanly.
class TestBackupService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void serviceBacksUpOffThreadAndPrunes();
    void serviceRestoresBackupOverwrite();

private:
    QTemporaryDir m_configDir;
};

void TestBackupService::initTestCase()
{
    // Isolate KConfig so the test never touches the real egoboardrc.
    QVERIFY(m_configDir.isValid());
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
}

void TestBackupService::serviceBacksUpOffThreadAndPrunes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("history.db"));
    {
        StorageManager storage(dbPath);
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("backup-entry");
        record.type = ContentType::Text;
        record.textData = QStringLiteral("backed up payload");
        record.preview = record.textData;
        record.timestamp = 1000;
        QVERIFY(storage.insertOrUpdate(record) > 0);
    }

    SettingsManager settings;
    settings.setBackupsEnabled(true);
    settings.setBackupFolder(dir.filePath(QStringLiteral("backups")));
    settings.setBackupKeep(1);

    BackupService service(dbPath, &settings);
    QSignalSpy spy(&service, &BackupService::finished);

    // Enabling backups and starting the service writes the first backup right
    // away instead of waiting a day.
    service.start();
    QVERIFY(spy.wait(15000));
    QCOMPARE(spy.count(), 1);
    QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(2).toString()));
    QVERIFY(QFile::exists(spy.first().at(1).toString()));
    QVERIFY(settings.lastBackupMs() > 0);

    // A manual run works and pruning keeps only the newest file.
    QVERIFY(service.runNow());
    QVERIFY(spy.wait(15000));
    QCOMPARE(spy.count(), 2);
    QVERIFY2(spy.at(1).at(0).toBool(), qPrintable(spy.at(1).at(2).toString()));
    const QStringList backups = ExportImportManager::listBackups(settings.backupFolder());
    QCOMPARE(backups.size(), 1);
    QCOMPARE(backups.first(), spy.at(1).at(1).toString());

    // The file is a complete export: importing it into a fresh database
    // restores the entry.
    StorageManager restored(dir.filePath(QStringLiteral("restored.db")));
    BookmarkManager bookmarks(restored.database());
    SnippetManager snippets(restored.database());
    ExportImportManager io(&restored, &bookmarks, &snippets);
    const auto imported =
        io.importFromFile(backups.first(), ExportImportManager::ImportMode::Merge);
    QVERIFY2(imported.ok, qPrintable(imported.error));
    QCOMPARE(imported.entriesImported, 1);
    QCOMPARE(restored.stats().entryCount, qint64(1));
}

void TestBackupService::serviceRestoresBackupOverwrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("history.db"));
    const auto insert = [](const QString &path, const char *hash, const QString &text) {
        StorageManager storage(path);
        ClipboardRecord record;
        record.hash = QByteArray(hash);
        record.type = ContentType::Text;
        record.textData = text;
        record.preview = text;
        record.timestamp = 1000;
        return storage.insertOrUpdate(record);
    };
    QVERIFY(insert(dbPath, "before-restore", QStringLiteral("original entry")) > 0);

    SettingsManager settings;
    settings.setBackupsEnabled(true);
    settings.setBackupFolder(dir.filePath(QStringLiteral("backups")));
    settings.setBackupKeep(5);

    BackupService service(dbPath, &settings);
    QSignalSpy backupSpy(&service, &BackupService::finished);
    service.start();
    QVERIFY(service.runNow());
    QVERIFY(backupSpy.wait(15000));
    QVERIFY2(backupSpy.first().at(0).toBool(), qPrintable(backupSpy.first().at(2).toString()));
    const QString backupPath = backupSpy.first().at(1).toString();
    QVERIFY(QFile::exists(backupPath));

    // Diverge from the backup: drop everything and add a different entry.
    {
        StorageManager storage(dbPath);
        QCOMPARE(storage.clearHistory(true), 1);
        QVERIFY(storage.insertOrUpdate([&] {
            ClipboardRecord record;
            record.hash = QByteArrayLiteral("after-backup");
            record.type = ContentType::Text;
            record.textData = QStringLiteral("different entry");
            record.preview = record.textData;
            record.timestamp = 2000;
            return record;
        }()) > 0);
    }

    // Restoring replaces the current history with the backed-up state.
    QSignalSpy restoreSpy(&service, &BackupService::restoreFinished);
    QVERIFY(service.restoreNow(backupPath, ExportImportManager::ImportMode::Overwrite));
    QVERIFY(restoreSpy.wait(15000));
    QCOMPARE(restoreSpy.count(), 1);
    QVERIFY2(restoreSpy.first().at(0).toBool(), qPrintable(restoreSpy.first().at(2).toString()));
    QCOMPARE(restoreSpy.first().at(3).toInt(), 1); // entries imported

    StorageManager restored(dbPath);
    QCOMPARE(restored.stats().entryCount, qint64(1));
    FilterSpec filter;
    filter.searchText = QStringLiteral("original");
    const auto hits = restored.fetchPage(filter, {}, 10);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().hash, QByteArrayLiteral("before-restore"));

    // A missing file fails cleanly instead of crashing the worker.
    QSignalSpy failedSpy(&service, &BackupService::restoreFinished);
    QVERIFY(service.restoreNow(dir.filePath(QStringLiteral("nope.json")),
                               ExportImportManager::ImportMode::Merge));
    QVERIFY(failedSpy.wait(15000));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toBool(), false);
}

QTEST_GUILESS_MAIN(TestBackupService)
#include "tst_backupservice.moc"
