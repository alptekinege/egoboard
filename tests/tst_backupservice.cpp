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

QTEST_GUILESS_MAIN(TestBackupService)
#include "tst_backupservice.moc"
