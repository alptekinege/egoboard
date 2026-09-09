#pragma once

#include <QObject>

class QAction;

// Registers the global shortcuts through KGlobalAccel (so they work system
// wide, are configurable in Plasma's shortcut editor and survive on Wayland).
//   toggle     - show/hide the history window   (default Meta+V)
//   quickpaste - quick paste popup               (default Meta+Shift+V)
//   deletelast - drop the newest capture          (default Meta+Shift+D)
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject *parent = nullptr);

    QAction *toggleAction() const { return m_toggle; }
    QAction *quickPasteAction() const { return m_quickPaste; }
    QAction *deleteLastAction() const { return m_deleteLast; }

    static QList<QKeySequence> defaultToggleShortcut();
    static QList<QKeySequence> defaultQuickPasteShortcut();
    static QList<QKeySequence> defaultDeleteLastShortcut();

signals:
    void toggleRequested();
    void quickPasteRequested();
    void deleteLastRequested();

private:
    QAction *m_toggle = nullptr;
    QAction *m_quickPaste = nullptr;
    QAction *m_deleteLast = nullptr;
};
