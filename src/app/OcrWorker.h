#pragma once

#include <QObject>
#include <QImage>

class IClipboardStorage;

// Local-only OCR via the system `tesseract` binary (no network).
// Runs in a thread-pool worker, emits recognized text, and never blocks the GUI.
class OcrWorker : public QObject {
    Q_OBJECT
public:
    explicit OcrWorker(IClipboardStorage *storage, QObject *parent = nullptr);

    // True when `tesseract` is found in PATH and can be executed.
    static bool isAvailable();
    // Queue an image for OCR. Results arrive via recognized()/failed().
    void recognize(qint64 entryId, const QImage &image);
    void recognize(qint64 entryId, const QByteArray &pngData);

signals:
    void recognized(qint64 entryId, QString text);
    void failed(qint64 entryId, QString reason);

private:
    IClipboardStorage *m_storage = nullptr;
};
