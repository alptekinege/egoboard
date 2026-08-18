#pragma once

#include "ClipboardRecord.h"

#include <QClipboard>
#include <QObject>
#include <QTimer>

class IActiveWindowTracker;
class SettingsManager;

// Monitors QClipboard (both X11 and KWin Wayland sessions), classifies the
// content, applies size/sensitive-data policy and emits finished records for
// storage. Bursts of clipboard updates (apps setting several formats) are
// coalesced by a debounce timer.
class ClipboardWatcher : public QObject {
    Q_OBJECT
public:
    ClipboardWatcher(QClipboard *clipboard, SettingsManager *settings,
                     IActiveWindowTracker *activeWindow, QObject *parent = nullptr);

    void start();
    void setDebounceInterval(int ms) { m_debounce.setInterval(qBound(50, ms, 5000)); }

    // Suppress the next clipboard change(s) for a short while; used when
    // egoboard itself re-sets the clipboard for paste-back.
    void suppressOwnSets();

signals:
    void captured(const ClipboardRecord &record);
    void excludedSensitive(const QString &reason);
    void suppressedOwnChange();

private:
    void onClipboardChanged(QClipboard::Mode mode);
    void processPending();
    ClipboardRecord buildRecord(const QMimeData *mimeData) const;

    QClipboard *m_clipboard = nullptr;
    SettingsManager *m_settings = nullptr;
    IActiveWindowTracker *m_activeWindow = nullptr;
    QTimer m_debounce;
    QClipboard::Mode m_pendingMode = QClipboard::Clipboard;
    qint64 m_suppressUntilEpochMs = 0;
};
