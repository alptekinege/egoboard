#pragma once

#include <QObject>

class QAction;

// Registers the global shortcuts through KGlobalAccel (so they work system
// wide, are configurable in Plasma's shortcut editor and survive on Wayland).
//   toggle     - show/hide the history window   (default Meta+V)
//   quickpaste - quick paste popup               (default Meta+Shift+V)
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject *parent = nullptr);

    QAction *toggleAction() const { return m_toggle; }
    QAction *quickPasteAction() const { return m_quickPaste; }

    static QList<QKeySequence> defaultToggleShortcut();
    static QList<QKeySequence> defaultQuickPasteShortcut();

signals:
    void toggleRequested();
    void quickPasteRequested();

private:
    QAction *m_toggle = nullptr;
    QAction *m_quickPaste = nullptr;
};
