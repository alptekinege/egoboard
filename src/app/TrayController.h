#pragma once

#include "ClipboardRecord.h"

#include <QObject>

class KStatusNotifierItem;
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

signals:
    void toggleRequested();
    void quickPasteRequested();
    void settingsRequested();
    void clearRequested();
    void quitRequested();
    void pasteRequested(qint64 entryId);

private:
    void rebuildMenu();

    StorageManager *m_storage = nullptr;
    KStatusNotifierItem *m_sni = nullptr;
    QSystemTrayIcon *m_fallbackIcon = nullptr;
    QMenu *m_menu = nullptr;
    static constexpr int kRecentCount = 8;
};
