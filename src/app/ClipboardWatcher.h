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

    // Marks one upcoming clipboard write as egoboard's own (paste-back), so
    // the change event it causes is not recorded as a new history entry.
    // A token is consumed by the next change event, bounded by a short grace
    // window, and emitting suppressedOwnChange() when it is.
    void suppressOwnSets();

signals:
    void captured(const ClipboardRecord &record);
    void excludedSensitive(const QString &reason);
    void redactedSensitive(const QString &kinds); // Redact mode: secrets replaced
    void suppressedOwnChange();

private slots:
    void processPending();
    void onClipboardChanged(QClipboard::Mode mode);

private:
    ClipboardRecord buildRecord(const QMimeData *mimeData) const;

    QClipboard *m_clipboard = nullptr;
    SettingsManager *m_settings = nullptr;
    IActiveWindowTracker *m_activeWindow = nullptr;
    QTimer m_debounce;
    QClipboard::Mode m_pendingMode = QClipboard::Clipboard;
    int m_pendingSelfSets = 0;
    qint64 m_selfSetDeadlineMs = 0;
};
