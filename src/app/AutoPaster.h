#pragma once

#include "ClipboardRecord.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

class ClipboardWatcher;
class PortalPaster;
class SettingsManager;

// Paste-back flow: rebuilds full QMimeData for the record, puts it on the
// clipboard, hides egoboard's window, then either simulates Ctrl+V (X11 via
// XTest, falling back to the xdotool binary), asks the compositor to press it
// (Wayland portal, opt-in and session-live only), or shows a passive
// notification (Wayland fallback, where key injection is not permitted).
class AutoPaster : public QObject {
    Q_OBJECT
public:
    explicit AutoPaster(ClipboardWatcher *watcher, QObject *parent = nullptr);
    ~AutoPaster() override;

    // Restores the record to the clipboard without hiding a window or
    // simulating a paste keystroke.
    bool copyToClipboard(const ClipboardRecord &record);
    void paste(const ClipboardRecord &record, QWidget *windowToHide = nullptr);
    // Opt-in portal source (plus the flag behind it); null keeps the
    // historical X11/notification behavior exactly.
    void setPortalPaster(PortalPaster *portal) { m_portal = portal; }
    void setSettingsManager(SettingsManager *settings) { m_settings = settings; }

    static bool canSimulateKeys();

signals:
    void pasted(qint64 id);
    void failed(const QString &reason);

private:
    void simulateCtrlV();
    bool xtestPaste();
    bool xdotoolPaste();

    ClipboardWatcher *m_watcher = nullptr;
    PortalPaster *m_portal = nullptr; // opt-in Wayland RemoteDesktop source
    SettingsManager *m_settings = nullptr; // portal opt-in flag behind it
    bool m_xdotoolAvailable = false;
    bool m_xdotoolChecked = false;
    // Reused X connection (XOpenDisplay is a round trip; pasting is frequent).
    // Stored as void* so Xlib stays out of this header.
    void *m_x11Display = nullptr;
};
