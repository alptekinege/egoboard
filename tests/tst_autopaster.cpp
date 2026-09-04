#include <QtTest>

#include "AutoPaster.h"

#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

class TestAutoPaster : public QObject
{
    Q_OBJECT

private slots:
    void copiesTextPayload();
    void copiesRichTextWithPlainTextFallback();
    void copiesFilePayload();
    void copiesValidImagePayload();
    void rejectsMissingAndInvalidImagePayloads();
};

void TestAutoPaster::copiesTextPayload()
{
    AutoPaster paster(nullptr);
    ClipboardRecord record;
    record.id = 1;
    record.type = ContentType::Text;
    record.textData = QStringLiteral("hello clipboard");

    QSignalSpy failedSpy(&paster, &AutoPaster::failed);
    QVERIFY(paster.copyToClipboard(record));
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello clipboard"));
}

void TestAutoPaster::copiesRichTextWithPlainTextFallback()
{
    AutoPaster paster(nullptr);
    ClipboardRecord record;
    record.type = ContentType::RichText;
    record.textData = QStringLiteral("<p><b>Hello</b> <i>world</i></p>");

    QVERIFY(paster.copyToClipboard(record));
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasHtml());
    QVERIFY(mime->html().contains(QStringLiteral("<b>")));
    QCOMPARE(mime->text(), QStringLiteral("Hello world"));
}

void TestAutoPaster::copiesFilePayload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = dir.filePath(QStringLiteral("first.txt"));
    const QString second = dir.filePath(QStringLiteral("second.txt"));
    QVERIFY(QFile(first).open(QIODevice::WriteOnly));
    QVERIFY(QFile(second).open(QIODevice::WriteOnly));

    AutoPaster paster(nullptr);
    ClipboardRecord record;
    record.type = ContentType::Files;
    const QJsonArray paths{first, second};
    record.textData = QString::fromUtf8(QJsonDocument(paths).toJson(QJsonDocument::Compact));

    QVERIFY(paster.copyToClipboard(record));
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasUrls());
    const QList<QUrl> expectedUrls = {QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)};
    QCOMPARE(mime->urls(), expectedUrls);
    QCOMPARE(mime->text(), first + QLatin1Char('\n') + second);
}

void TestAutoPaster::copiesValidImagePayload()
{
    QImage image(2, 3, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#3daee9")));
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));

    AutoPaster paster(nullptr);
    ClipboardRecord record;
    record.type = ContentType::Image;
    record.blobData = png;
    record.hasBlob = true;

    QVERIFY(paster.copyToClipboard(record));
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasImage());
    const QImage restored = qvariant_cast<QImage>(mime->imageData());
    QCOMPARE(restored.size(), QSize(2, 3));
    QCOMPARE(restored.pixelColor(0, 0), QColor(QStringLiteral("#3daee9")));
}

void TestAutoPaster::rejectsMissingAndInvalidImagePayloads()
{
    AutoPaster paster(nullptr);
    QSignalSpy failedSpy(&paster, &AutoPaster::failed);

    ClipboardRecord missing;
    missing.type = ContentType::Image;
    QVERIFY(!paster.copyToClipboard(missing));
    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(failedSpy.first().first().toString().contains(QStringLiteral("no stored payload")));

    ClipboardRecord invalid;
    invalid.type = ContentType::Image;
    invalid.hasBlob = true;
    invalid.blobData = QByteArrayLiteral("not a PNG");
    QVERIFY(!paster.copyToClipboard(invalid));
    QCOMPARE(failedSpy.count(), 2);
}

QTEST_MAIN(TestAutoPaster)
#include "tst_autopaster.moc"
