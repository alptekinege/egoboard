#include "OcrWorker.h"

#include "IClipboardStorage.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent>

OcrWorker::OcrWorker(IClipboardStorage *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

bool OcrWorker::isAvailable()
{
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    return !bin.isEmpty();
}

void OcrWorker::recognize(qint64 entryId, const QImage &image)
{
    if (entryId == 0 || image.isNull())
        return;
    if (!isAvailable()) {
        emit failed(entryId, tr("tesseract not found"));
        return;
    }
    // Copy image for the worker thread
    QImage copy = image;
    QtConcurrent::run([this, entryId, copy]() {
        QTemporaryDir dir;
        if (!dir.isValid()) {
            QMetaObject::invokeMethod(this, [this, entryId]{
                emit failed(entryId, tr("cannot create temp dir"));
            }, Qt::QueuedConnection);
            return;
        }
        const QString pngPath = dir.filePath(QStringLiteral("ocr.png"));
        QImage img = copy;
        // Tesseract works better with a bit of scaling for tiny clipboard images
        if (qMin(img.width(), img.height()) < 200) {
            img = img.scaled(img.width()*2, img.height()*2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (!img.save(pngPath, "PNG")) {
            QMetaObject::invokeMethod(this, [this, entryId]{
                emit failed(entryId, tr("cannot save temp image"));
            }, Qt::QueuedConnection);
            return;
        }
        QProcess proc;
        // --psm 6: assume uniform block of text; --oem 1: LSTM only
        proc.start(QStringLiteral("tesseract"), {pngPath, QStringLiteral("stdout"), QStringLiteral("-l"), QStringLiteral("eng"), QStringLiteral("--psm"), QStringLiteral("6"), QStringLiteral("--oem"), QStringLiteral("1")});
        if (!proc.waitForStarted(2000)) {
            QMetaObject::invokeMethod(this, [this, entryId]{
                emit failed(entryId, tr("tesseract failed to start"));
            }, Qt::QueuedConnection);
            return;
        }
        if (!proc.waitForFinished(15000)) {
            proc.kill();
            QMetaObject::invokeMethod(this, [this, entryId]{
                emit failed(entryId, tr("tesseract timeout"));
            }, Qt::QueuedConnection);
            return;
        }
        QString out;
        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
            out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            // Tesseract often adds form-feed; strip
            out.remove(QChar(0x0C));
            out = out.trimmed();
        } else {
            const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            QMetaObject::invokeMethod(this, [this, entryId, err]{
                emit failed(entryId, err.isEmpty() ? tr("tesseract error") : err);
            }, Qt::QueuedConnection);
            return;
        }
        if (out.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, entryId]{
                emit failed(entryId, tr("no text recognized"));
            }, Qt::QueuedConnection);
            return;
        }
        // Cap length to avoid DB bloat
        if (out.size() > 8000) out = out.left(8000);
        QMetaObject::invokeMethod(this, [this, entryId, out]{
            emit recognized(entryId, out);
        }, Qt::QueuedConnection);
    });
}

void OcrWorker::recognize(qint64 entryId, const QByteArray &pngData)
{
    QImage img;
    if (!pngData.isEmpty()) img.loadFromData(pngData, "PNG");
    recognize(entryId, img);
}
