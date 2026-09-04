#include <QtTest>

#include "OcrWorker.h"

#include <QImage>
#include <QSignalSpy>

class TestOcrWorker : public QObject
{
    Q_OBJECT

private slots:
    void ignoresInvalidRequests();
    void ignoresMalformedPngData();
    void reportsAvailabilityForValidImage();
};

void TestOcrWorker::ignoresInvalidRequests()
{
    OcrWorker worker(nullptr);
    QSignalSpy recognizedSpy(&worker, &OcrWorker::recognized);
    QSignalSpy failedSpy(&worker, &OcrWorker::failed);

    worker.recognize(0, QImage(10, 10, QImage::Format_RGB32));
    worker.recognize(42, QImage());
    worker.recognize(0, QByteArrayLiteral("not png"));
    worker.recognize(42, QByteArray());

    QTest::qWait(50);
    QCOMPARE(recognizedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
}

void TestOcrWorker::ignoresMalformedPngData()
{
    OcrWorker worker(nullptr);
    QSignalSpy recognizedSpy(&worker, &OcrWorker::recognized);
    QSignalSpy failedSpy(&worker, &OcrWorker::failed);

    worker.recognize(7, QByteArrayLiteral("this is not a PNG"));
    QTest::qWait(50);
    QCOMPARE(recognizedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
}

void TestOcrWorker::reportsAvailabilityForValidImage()
{
    OcrWorker worker(nullptr);
    worker.setLanguage(QStringLiteral("  eng  "));
    worker.setMaxChars(1);

    QImage image(20, 20, QImage::Format_RGB32);
    image.fill(Qt::white);
    QSignalSpy recognizedSpy(&worker, &OcrWorker::recognized);
    QSignalSpy failedSpy(&worker, &OcrWorker::failed);

    worker.recognize(99, image);
    if (!OcrWorker::isAvailable()) {
        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 1000);
        QCOMPARE(failedSpy.first().at(0).toLongLong(), qint64(99));
        QVERIFY(!failedSpy.first().at(1).toString().isEmpty());
        QCOMPARE(recognizedSpy.count(), 0);
        return;
    }

    // OCR availability is environment-dependent. If tesseract is present,
    // assert only the result contract and avoid depending on installed data
    // or exact recognition output.
    QTRY_VERIFY_WITH_TIMEOUT(recognizedSpy.count() + failedSpy.count() >= 1, 20000);
    if (recognizedSpy.count() > 0) {
        QCOMPARE(recognizedSpy.first().at(0).toLongLong(), qint64(99));
        QVERIFY(recognizedSpy.first().at(1).toString().size() <= 1);
    } else {
        QCOMPARE(failedSpy.first().at(0).toLongLong(), qint64(99));
        QVERIFY(!failedSpy.first().at(1).toString().isEmpty());
    }
}

QTEST_GUILESS_MAIN(TestOcrWorker)
#include "tst_ocrworker.moc"
