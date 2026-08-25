#pragma once

#include "ExpirePolicy.h"

#include <QObject>
#include <QTimer>

class SettingsManager;
class StorageManager;

// Applies SettingsManager::expireRules() on a schedule — at startup, every
// 15 minutes, and (debounced) shortly after clipboard captures — using the
// core StorageManager::expireEntries() SQL. Emits expired(count) when rows
// were deleted so the shell can show a notification.
class ExpireScheduler : public QObject {
    Q_OBJECT
public:
    ExpireScheduler(StorageManager *storage, SettingsManager *settings, QObject *parent = nullptr);

    void start();

public slots:
    void applyRules();
    // Restarts the trailing-edge debounce (call from capture signals).
    void scheduleAfterCapture();

signals:
    void expired(int count);

private:
    StorageManager *m_storage = nullptr;
    SettingsManager *m_settings = nullptr;
    QTimer m_timer;
    QTimer m_captureDebounce;
};