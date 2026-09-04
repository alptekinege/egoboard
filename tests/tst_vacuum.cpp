#include <QtTest>

#include "StorageManager.h"
#include "VacuumWorker.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestVacuum : public QObject
{
    Q_OBJECT

private slots:
    void vacuumsExistingDatabase();
    void reportsMissingDatabase();
    void canRunRepeatedly();
};

void TestVacuum::vacuumsExistingDatabase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("vacuum.db"));
    {
        StorageManager storage(path);
        for (int i = 0; i < 20; ++i) {
            ClipboardRecord record;
            record.hash = QByteArrayLiteral("vacuum-") + QByteArray::number(i);
            record.textData = QStringLiteral("payload %1").arg(i);
            record.preview = record.textData;
            record.timestamp = i + 1;
            record.sizeBytes = record.textData.toUtf8().size();
            QVERIFY(storage.insertOrUpdate(record) > 0);
        }
    }
    QVERIFY(QFile::exists(path));

    VacuumWorker worker(path);
    QSignalSpy spy(&worker, &VacuumWorker::finished);
    worker.run();
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.first().at(0).toBool());
    QVERIFY(spy.first().at(1).toLongLong() >= 0);
    QVERIFY(QFile::exists(path));

    StorageManager reopened(path);
    QCOMPARE(reopened.stats().entryCount, qint64(20));
}

void TestVacuum::reportsMissingDatabase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("missing/subdir/vacuum.db"));

    VacuumWorker worker(path);
    QSignalSpy spy(&worker, &VacuumWorker::finished);
    worker.run();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.first().at(0).toBool());
    QCOMPARE(spy.first().at(1).toLongLong(), qint64(0));
}

void TestVacuum::canRunRepeatedly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("repeat.db"));
    StorageManager storage(path);
    ClipboardRecord record;
    record.hash = QByteArrayLiteral("repeat");
    record.textData = QStringLiteral("repeatable");
    record.preview = record.textData;
    record.timestamp = 1;
    QVERIFY(storage.insertOrUpdate(record) > 0);

    VacuumWorker first(path);
    QSignalSpy firstSpy(&first, &VacuumWorker::finished);
    first.run();
    QCOMPARE(firstSpy.count(), 1);
    QVERIFY(firstSpy.first().at(0).toBool());

    VacuumWorker second(path);
    QSignalSpy secondSpy(&second, &VacuumWorker::finished);
    second.run();
    QCOMPARE(secondSpy.count(), 1);
    QVERIFY(secondSpy.first().at(0).toBool());
}

QTEST_GUILESS_MAIN(TestVacuum)
#include "tst_vacuum.moc"
