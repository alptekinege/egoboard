#include <QtTest>

#include "../src/app/BackupService.h"
#include "../src/app/SettingsManager.h"
#include "BookmarkManager.h"
#include "ExportImportManager.h"
#include "SnippetManager.h"
#include "StorageManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
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
    void backupPackageRoundTripsDatabaseAndSettings();
    void backupCancelLeavesNoPartialPackage();
    void corruptPackageIsRejectedBeforeOverwrite();
    void backupFormatUsesAvailableDependencies();
    void fiftyThousandEntryBackupRestoreStaysWithinExportBudget();

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

    // New packages are discoverable alongside legacy JSON backups.
    QVERIFY(QFileInfo(backups.first()).suffix() == QLatin1String("zip")
            || QFileInfo(backups.first()).suffix() == QLatin1String("backup"));
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

void TestBackupService::backupPackageRoundTripsDatabaseAndSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("history.db"));
    {
        StorageManager storage(dbPath);
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("bundle-source");
        record.type = ContentType::Text;
        record.textData = QStringLiteral("sensitive backup payload");
        record.preview = record.textData;
        record.timestamp = 1000;
        record.sensitive = true;
        QVERIFY(storage.insertOrUpdate(record) > 0);
    }

    SettingsManager settings;
    settings.setBackupsEnabled(false);
    settings.setBackupFolder(dir.filePath(QStringLiteral("backups")));
    settings.setStartVisible(false);
    BackupService service(dbPath, &settings);
    service.start();
    QSignalSpy backupSpy(&service, &BackupService::finished);
    QVERIFY(service.runNow());
    QVERIFY(backupSpy.wait(15000));
    QVERIFY2(backupSpy.first().at(0).toBool(), qPrintable(backupSpy.first().at(2).toString()));
    const QString backupPath = backupSpy.first().at(1).toString();
    QVERIFY(QFileInfo::exists(backupPath));

    settings.setStartVisible(true);
    {
        StorageManager storage(dbPath);
        QCOMPARE(storage.clearHistory(true), 1);
    }

    QSignalSpy restoreSpy(&service, &BackupService::restoreFinished);
    QVERIFY(service.restoreNow(backupPath, ExportImportManager::ImportMode::Overwrite, true));
    QVERIFY(restoreSpy.wait(15000));
    QCOMPARE(restoreSpy.count(), 1);
    QVERIFY2(restoreSpy.first().at(0).toBool(), qPrintable(restoreSpy.first().at(2).toString()));
    QCOMPARE(settings.startVisible(), false);

    StorageManager restored(dbPath);
    QCOMPARE(restored.stats().entryCount, qint64(1));
    FilterSpec filter;
    filter.searchText = QStringLiteral("sensitive backup payload");
    const auto hits = restored.fetchPage(filter, {}, 10);
    QCOMPARE(hits.size(), 1);
    QVERIFY(hits.first().sensitive);
}

void TestBackupService::backupCancelLeavesNoPartialPackage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("history.db"));
    {
        StorageManager storage(dbPath);
        for (int i = 0; i < 100; ++i) {
            ClipboardRecord record;
            record.hash = QByteArrayLiteral("cancel-") + QByteArray::number(i);
            record.type = ContentType::Text;
            record.textData = QString(4096, QLatin1Char('x'));
            record.preview = record.textData.left(100);
            record.timestamp = 1000 + i;
            QVERIFY(storage.insertOrUpdate(record) > 0);
        }
    }
    SettingsManager settings;
    settings.setBackupsEnabled(false);
    settings.setBackupFolder(dir.filePath(QStringLiteral("backups")));
    BackupService service(dbPath, &settings);
    service.start();
    QSignalSpy finishedSpy(&service, &BackupService::finished);
    QVERIFY(service.runNow());
    service.cancel();
    QVERIFY(finishedSpy.wait(15000));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toBool(), false);
    QVERIFY(ExportImportManager::listBackups(settings.backupFolder()).isEmpty());
    const QStringList leftovers = QDir(settings.backupFolder()).entryList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    QVERIFY(leftovers.isEmpty());
}

void TestBackupService::corruptPackageIsRejectedBeforeOverwrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("history.db"));
    {
        StorageManager storage(dbPath);
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("must-survive-corrupt-restore");
        record.type = ContentType::Text;
        record.textData = QStringLiteral("keep me");
        record.preview = record.textData;
        record.timestamp = 1000;
        QVERIFY(storage.insertOrUpdate(record) > 0);
    }
    SettingsManager settings;
    settings.setBackupsEnabled(false);
    settings.setBackupFolder(dir.filePath(QStringLiteral("backups")));
    BackupService service(dbPath, &settings);
    service.start();

    QVERIFY(QDir().mkpath(settings.backupFolder()));
    const QString corruptPath = QDir(settings.backupFolder()).filePath(
        QStringLiteral("egoboard-backup-corrupt.zip"));
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QCOMPARE(corrupt.write(QByteArrayLiteral("not a ZIP archive")), qint64(17));
    corrupt.close();

    QSignalSpy restoreSpy(&service, &BackupService::restoreFinished);
    QVERIFY(service.restoreNow(corruptPath, ExportImportManager::ImportMode::Overwrite));
    QVERIFY(restoreSpy.wait(15000));
    QCOMPARE(restoreSpy.first().at(0).toBool(), false);
    StorageManager unchanged(dbPath);
    QCOMPARE(unchanged.stats().entryCount, qint64(1));
    FilterSpec filter;
    filter.searchText = QStringLiteral("keep me");
    QCOMPARE(unchanged.fetchPage(filter, {}, 10).size(), 1);
}

void TestBackupService::backupFormatUsesAvailableDependencies()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("history.db"));
    {
        StorageManager storage(dbPath);
        ClipboardRecord record;
        record.hash = QByteArrayLiteral("format-probe");
        record.type = ContentType::Text;
        record.textData = QStringLiteral("backup format");
        record.preview = record.textData;
        record.timestamp = 1000;
        QVERIFY(storage.insertOrUpdate(record) > 0);
    }
    SettingsManager settings;
    settings.setBackupsEnabled(false);
    settings.setBackupFolder(dir.filePath(QStringLiteral("backups")));
    BackupService service(dbPath, &settings);
    service.start();
    QSignalSpy finishedSpy(&service, &BackupService::finished);
    QVERIFY(service.runNow());
    QVERIFY(finishedSpy.wait(15000));
    QVERIFY2(finishedSpy.first().at(0).toBool(), qPrintable(finishedSpy.first().at(2).toString()));
    const QString backupPath = finishedSpy.first().at(1).toString();
    QCOMPARE(QFileInfo(backupPath).suffix(),
             BackupService::zipBackupsAvailable() ? QStringLiteral("zip")
                                                  : QStringLiteral("backup"));
    QVERIFY(QFileInfo::exists(backupPath));
}

void TestBackupService::fiftyThousandEntryBackupRestoreStaysWithinExportBudget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("history.db"));
    {
        StorageManager storage(dbPath);
        QVERIFY(storage.beginBulk());
        for (int i = 0; i < 50000; ++i) {
            ClipboardRecord record;
            record.type = ContentType::Text;
            record.textData = QStringLiteral("backup scale entry %1 with searchable content")
                                  .arg(i);
            record.preview = record.textData;
            record.sizeBytes = record.textData.size();
            record.timestamp = 1700000000000LL + i;
            record.hash = QCryptographicHash::hash(record.textData.toUtf8(),
                                                   QCryptographicHash::Sha256).toHex();
            QVERIFY(storage.insertOrUpdate(record) > 0);
        }
        QVERIFY(storage.endBulk(true));
        QCOMPARE(storage.stats().entryCount, qint64(50000));
    }

    SettingsManager settings;
    settings.setBackupsEnabled(false);
    settings.setBackupFolder(dir.filePath(QStringLiteral("backups")));
    BackupService service(dbPath, &settings);
    service.start();
    QSignalSpy backupProgressSpy(&service, &BackupService::backupProgress);
    QSignalSpy backupSpy(&service, &BackupService::finished);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(service.runNow());
    QVERIFY(backupSpy.wait(15000));
    const qint64 backupMs = timer.elapsed();
    QVERIFY2(backupSpy.first().at(0).toBool(), qPrintable(backupSpy.first().at(2).toString()));
    QVERIFY(backupMs <= 10000); // same 10 s floor as the 50k JSON-export bench budget
    QVERIFY(backupProgressSpy.count() > 0);

    {
        StorageManager storage(dbPath);
        QCOMPARE(storage.clearHistory(true), 50000);
    }
    QSignalSpy restoreProgressSpy(&service, &BackupService::restoreProgress);
    QSignalSpy restoreSpy(&service, &BackupService::restoreFinished);
    timer.restart();
    QVERIFY(service.restoreNow(backupSpy.first().at(1).toString(),
                               ExportImportManager::ImportMode::Overwrite));
    QVERIFY(restoreSpy.wait(15000));
    const qint64 restoreMs = timer.elapsed();
    QVERIFY2(restoreSpy.first().at(0).toBool(), qPrintable(restoreSpy.first().at(2).toString()));
    QCOMPARE(restoreSpy.first().at(3).toInt(), 50000);
    QVERIFY(restoreMs <= 10000);
    QVERIFY(restoreProgressSpy.count() > 0);

    bool canceledInFlight = false;
    const QMetaObject::Connection cancelOnProgress = connect(
        &service, &BackupService::backupProgress, &service,
        [&service, &canceledInFlight](int, int) {
            if (!canceledInFlight) {
                canceledInFlight = true;
                service.cancel();
            }
        }, Qt::DirectConnection);
    QSignalSpy canceledSpy(&service, &BackupService::finished);
    QVERIFY(service.runNow());
    QVERIFY(canceledSpy.wait(15000));
    disconnect(cancelOnProgress);
    QCOMPARE(canceledInFlight, true);
    QCOMPARE(canceledSpy.first().at(0).toBool(), false);
    QCOMPARE(ExportImportManager::listBackups(settings.backupFolder()).size(), 1);

    {
        StorageManager storage(dbPath);
        QCOMPARE(storage.clearHistory(true), 50000);
    }
    bool canceledRestoreInFlight = false;
    const QMetaObject::Connection cancelRestoreOnProgress = connect(
        &service, &BackupService::restoreProgress, &service,
        [&service, &canceledRestoreInFlight](int, int) {
            if (!canceledRestoreInFlight) {
                canceledRestoreInFlight = true;
                service.cancel();
            }
        }, Qt::DirectConnection);
    QSignalSpy canceledRestoreSpy(&service, &BackupService::restoreFinished);
    QVERIFY(service.restoreNow(backupSpy.first().at(1).toString(),
                               ExportImportManager::ImportMode::Overwrite));
    QVERIFY(canceledRestoreSpy.wait(15000));
    disconnect(cancelRestoreOnProgress);
    QCOMPARE(canceledRestoreInFlight, true);
    QCOMPARE(canceledRestoreSpy.first().at(0).toBool(), false);
    StorageManager unchanged(dbPath);
    QCOMPARE(unchanged.stats().entryCount, qint64(0));
    qInfo("P2-B 50k-entry ZIP/fallback backup: %lld ms, restore: %lld ms",
          backupMs, restoreMs);
}

QTEST_GUILESS_MAIN(TestBackupService)
#include "tst_backupservice.moc"
