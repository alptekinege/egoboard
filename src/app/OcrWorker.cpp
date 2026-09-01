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

void OcrWorker::setLanguage(const QString &lang)
{
    const QString v = lang.trimmed().isEmpty() ? QStringLiteral("eng") : lang.trimmed();
    // Direct assignment is safe because recognize() snapshots these values
    // before dispatching to the thread pool.
    m_language = v;
}

void OcrWorker::setMaxChars(int maxChars)
{
    const int capped = qBound(512, maxChars, 65536);
    m_maxChars = capped;
}

void OcrWorker::recognize(qint64 entryId, const QImage &image)
{
    if (entryId == 0 || image.isNull())
        return;
    if (!isAvailable()) {
        emit failed(entryId, tr("tesseract not found"));
        return;
    }
    // Copy image + current settings for the worker thread; hop results back
    // through a guarded queued invoke so shutdown cannot leave a dangling `this`.
    const QImage copy = image;
    const QString lang = m_language;
    const int maxChars = qMax(1, m_maxChars);
    QPointer<OcrWorker> guard(this);
    QtConcurrent::run([guard, entryId, copy, lang, maxChars]() {
        // All failures funnel through this helper: emit only while alive.
        auto fail = [guard](qint64 id, const QString &reason) {
            if (!guard) return;
            QMetaObject::invokeMethod(guard, [guard, id, reason] {
                emit guard->failed(id, reason);
            }, Qt::QueuedConnection);
        };
        QTemporaryDir dir;
        if (!dir.isValid()) {
            fail(entryId, OcrWorker::tr("cannot create temp dir"));
            return;
        }
        const QString pngPath = dir.filePath(QStringLiteral("ocr.png"));
        QImage img = copy;
        // Tesseract works better with a bit of scaling for tiny clipboard images
        if (qMin(img.width(), img.height()) < 200) {
            img = img.scaled(img.width()*2, img.height()*2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (!img.save(pngPath, "PNG")) {
            fail(entryId, OcrWorker::tr("cannot save temp image"));
            return;
        }
        QProcess proc;
        // --psm 6: assume uniform block of text; --oem 1: LSTM only
        proc.start(QStringLiteral("tesseract"), {pngPath, QStringLiteral("stdout"), QStringLiteral("-l"), lang, QStringLiteral("--psm"), QStringLiteral("6"), QStringLiteral("--oem"), QStringLiteral("1")});
        if (!proc.waitForStarted(2000)) {
            fail(entryId, OcrWorker::tr("tesseract failed to start"));
            return;
        }
        if (!proc.waitForFinished(15000)) {
            proc.kill();
            fail(entryId, OcrWorker::tr("tesseract timeout"));
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
            fail(entryId, err.isEmpty() ? OcrWorker::tr("tesseract error") : err);
            return;
        }
        if (out.isEmpty()) {
            fail(entryId, OcrWorker::tr("no text recognized"));
            return;
        }
        // Cap length to avoid DB bloat
        if (out.size() > maxChars) out = out.left(maxChars);
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, entryId, out] {
            emit guard->recognized(entryId, out);
        }, Qt::QueuedConnection);
    });
}

void OcrWorker::recognize(qint64 entryId, const QByteArray &pngData)
{
    QImage img;
    if (!pngData.isEmpty()) img.loadFromData(pngData, "PNG");
    recognize(entryId, img);
}
