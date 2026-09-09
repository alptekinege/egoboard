#pragma once

#include <QPoint>

#include <functional>

// Resolves the GLOBAL cursor position, which Wayland hides from clients:
// QCursor::pos() only knows where the pointer last touched OUR windows, so
// popups triggered by a global hotkey would anchor to a stale position.
//
// On Wayland + KWin we ask the compositor: a tiny helper script (KWin
// scripting API) reports workspace.cursorPos back over D-Bus (see
// EgoboardDbusAdaptor::ReportCursorPos). Everywhere else — X11 (exact) and
// non-KWin Wayland (best effort) — the callback runs with QCursor::pos().
class KWinCursorTracker : public QObject {
    Q_OBJECT
public:
    // Runs callback on the GUI thread with the best cursor position available.
    // On KWin this waits up to timeoutMs for the compositor to answer.
    static void queryGlobal(const std::function<void(const QPoint &)> &callback,
                            int timeoutMs = 250);

    // Called by the D-Bus adaptor when KWin reports the position.
    static void reportGlobalPos(int x, int y);

private:
    explicit KWinCursorTracker(QObject *parent);
    static KWinCursorTracker *self();
    bool ensureScriptLoaded();
    bool runScript();

    std::function<void(const QPoint &)> m_callback;
    QTimer *m_timeoutTimer = nullptr;
    QString m_scriptPath;
    int m_scriptId = -1;
    bool m_kwinUnavailable = false; // org.kde.KWin missing: stop asking
};
