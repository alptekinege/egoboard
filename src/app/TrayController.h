#pragma once

#include "ClipboardRecord.h"

#include <QIcon>
#include <QObject>

class KStatusNotifierItem;
class QAction;
class QMenu;
class QSystemTrayIcon;
class SettingsManager;
class StorageManager;

// System tray integration: KStatusNotifierItem (StatusNotifierItem over DBus,
// native on Plasma for both X11 and Wayland) with a QSystemTrayIcon fallback
// for environments without an SNI host. Both surfaces share one lazily built
// menu (rebuilt on aboutToShow only), so their actions and labels always
// match. TrayMode is live: auto/Always/hidden is re-evaluated on every
// settings change and capture without a restart; paused and capture states
// refresh the tooltip and icon. What the primary and secondary clicks do, and
// whether the wheel walks the recent entries, comes from the settings (read
// on each event, so a change needs no reload).
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
    // Re-evaluates TrayMode against the history count; touches the SNI
    // status / fallback visibility only when the outcome changed.
    void applyVisibility();
    // Pause-aware tooltip + icon (theme icons only, no bundled artwork).
    void refreshTooltipAndIcon();
    // Runs the configured click action (fresh from the settings).
    void runClickAction(bool secondary);

    StorageManager *m_storage = nullptr;
    SettingsManager *m_settings = nullptr;
    KStatusNotifierItem *m_sni = nullptr;
    QSystemTrayIcon *m_fallbackIcon = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_pauseAction = nullptr;
    QIcon m_appIcon;
    QIcon m_pausedIcon;
    bool m_paused = false;
    bool m_visible = true;
    bool m_iconPaused = false;
    static constexpr int kRecentCount = 8;
};
