#pragma once

#include <QObject>

class QAction;

// Registers the global shortcuts through KGlobalAccel (so they work system
// wide, are configurable in Plasma's shortcut editor and survive on Wayland).
//   toggle     - show/hide the history window   (default Meta+Shift+V)
//   quickpaste - quick paste popup               (default Meta+V)
//   deletelast - drop the newest capture          (default Meta+Shift+D)
//   pause      - pause/resume clipboard capture   (default Meta+Shift+P)
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject *parent = nullptr);

    QAction *toggleAction() const { return m_toggle; }
    QAction *quickPasteAction() const { return m_quickPaste; }
    QAction *deleteLastAction() const { return m_deleteLast; }
    QAction *pauseAction() const { return m_pause; }

    static QList<QKeySequence> defaultToggleShortcut();
    static QList<QKeySequence> defaultQuickPasteShortcut();
    static QList<QKeySequence> defaultDeleteLastShortcut();
    static QList<QKeySequence> defaultPauseShortcut();

    // Keeps the checkable pause action in sync with the real capture state
    // (e.g. when the tray menu toggled it or the session locked).
    void setPaused(bool paused);

signals:
    void toggleRequested();
    void quickPasteRequested();
    void deleteLastRequested();
    void pauseToggleRequested(bool paused);

private:
    QAction *m_toggle = nullptr;
    QAction *m_quickPaste = nullptr;
    QAction *m_deleteLast = nullptr;
    QAction *m_pause = nullptr;
};
