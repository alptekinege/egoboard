#pragma once
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>

class QWindow;
class QScreen;

// Thin wrapper around LayerShellQt (KDE) for the QuickPaste popup.
// On Wayland + KWin it promotes the popup to a wlr-layer-shell overlay
// with exclusive keyboard grab, so number keys work without focus hacks.
// On X11 / offscreen / compositors without wlr-layer-shell it is a
// no-op and the caller falls back to cursor-anchored QWidget::move().
class LayerShellHelper : public QObject {
    Q_OBJECT
public:
    static bool isWayland();
    // True if the platform is wayland and LayerShellQt is linked in.
    // Offscreen/CI returns false — keeps tests headless.
    static bool isAvailable();
    // Human-readable one-liner for Settings diagnostics.
    static QString diagnostics();
    static QString platformName();

    // Configure `window` as a layer-shell overlay positioned near `cursorPos`
    // on `screen` with `desiredSize` (from QWidget::sizeHint). No-op when
    // !isAvailable(). Must be called before the window is shown (or while
    // it has a native handle); safe to call multiple times to reposition.
    static void configureForQuickPaste(QWindow *window, QScreen *screen,
                                       QSize desiredSize, QPoint cursorPos);
};
