#pragma once

#include "ContentType.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>

// One clipboard history entry. Instances fetched for the list view carry only
// the summary fields (textData/blobData empty, hasBlob tells whether the
// payload exists); fetchFull() fills the payload.
struct ClipboardRecord {
    qint64 id = 0;
    qint64 timestamp = 0; // ms since epoch
    ContentType type = ContentType::Text;
    QByteArray hash; // sha256 hex, used for deduplication
    QString textData; // plain text / html / JSON array of paths
    QByteArray blobData; // PNG bytes for images
    bool hasBlob = false;
    QString preview;
    qint64 sizeBytes = 0;
    bool pinned = false;
    bool sensitive = false;
    int useCount = 0;
    QString sourceApp;
    QString sourceWindow;
    QString ocrText;

    bool isValid() const { return id != 0; }
};

Q_DECLARE_METATYPE(ClipboardRecord)
