#pragma once

#include "ClipboardRecord.h"

#include <QObject>

class KStatusNotifierItem;
class QAction;
class QMenu;
class QSystemTrayIcon;
class SettingsManager;
class StorageManager;

// System tray integration: KStatusNotifierItem (StatusNotifierItem over DBus,
// native on Plasma for both X11 and Wayland) with a QSystemTrayIcon fallback
// for environments without an SNI host. The context menu shows the most
// recent entries for one-click paste-back; what the primary and secondary
// clicks do, and whether the wheel walks the recent entries, comes from the
// settings (read on each event, so a change needs no reload).
class TrayController : public QObject {
    Q_OBJECT
public:
    TrayController(StorageManager *storage, SettingsManager *settings, QObject *parent = nullptr);

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
    // Wheel over the icon: positive = towards older entries.
    void wheelSteps(int steps);

private:
    void rebuildMenu();
    // Runs the configured click action (fresh from the settings).
    void runClickAction(bool secondary);

    StorageManager *m_storage = nullptr;
    SettingsManager *m_settings = nullptr;
    KStatusNotifierItem *m_sni = nullptr;
    QSystemTrayIcon *m_fallbackIcon = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_pauseAction = nullptr;
    bool m_paused = false;
    static constexpr int kRecentCount = 8;
};
