#pragma once

#include "ClipboardRecord.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

class ClipboardWatcher;

// Paste-back flow: rebuilds full QMimeData for the record, puts it on the
// clipboard, hides egoboard's window, then either simulates Ctrl+V (X11 via
// XTest, falling back to the xdotool binary) or shows a passive notification
// (Wayland, where key injection from clients is not permitted).
class AutoPaster : public QObject {
    Q_OBJECT
public:
    explicit AutoPaster(ClipboardWatcher *watcher, QObject *parent = nullptr);

    void paste(const ClipboardRecord &record, QWidget *windowToHide = nullptr);

    static bool canSimulateKeys();

signals:
    void pasted(qint64 id);
    void failed(const QString &reason);

private:
    void simulateCtrlV();
    bool xtestPaste();
    bool xdotoolPaste();

    ClipboardWatcher *m_watcher = nullptr;
    bool m_xdotoolAvailable = false;
    bool m_xdotoolChecked = false;
};
