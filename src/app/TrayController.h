#pragma once

#include "ClipboardRecord.h"

#include <QObject>

class KStatusNotifierItem;
class QAction;
class QMenu;
class QSystemTrayIcon;
class StorageManager;

// System tray integration: KStatusNotifierItem (StatusNotifierItem over DBus,
// native on Plasma for both X11 and Wayland) with a QSystemTrayIcon fallback
// for environments without an SNI host. The context menu shows the most
// recent entries for one-click paste-back.
class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(StorageManager *storage, QObject *parent = nullptr);

    static bool statusNotifierHostAvailable();

    // Reflects the capture state on the checkable "Pause capture" menu entry.
    void setPaused(bool paused);

signals:
    void toggleRequested();
    void quickPasteRequested();
    void settingsRequested();
    void clearRequested();
    void quitRequested();
    void pasteRequested(qint64 entryId);
    void pauseToggled(bool paused);

private:
    void rebuildMenu();

    StorageManager *m_storage = nullptr;
    KStatusNotifierItem *m_sni = nullptr;
    QSystemTrayIcon *m_fallbackIcon = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_pauseAction = nullptr;
    bool m_paused = false;
    static constexpr int kRecentCount = 8;
};
