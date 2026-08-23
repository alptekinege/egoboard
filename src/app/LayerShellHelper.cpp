#include "LayerShellHelper.h"
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#ifdef EGOBOARD_HAVE_LAYERSHELLQT
#include <LayerShellQt/Window>
#endif

bool LayerShellHelper::isWayland()
{
    return QGuiApplication::platformName() == QLatin1String("wayland");
}

QString LayerShellHelper::platformName()
{
    return QGuiApplication::platformName();
}

bool LayerShellHelper::isAvailable()
{
#ifdef EGOBOARD_HAVE_LAYERSHELLQT
    if (!isWayland())
        return false;
    // LayerShellQt is available and platform is wayland; compositor
    // support for wlr-layer-shell is probed at bind time. If the
    // compositor lacks it, LayerShellQt silently falls back to a
    // normal window — we still report "available" (feature present),
    // but the popup will appear as a regular toplevel without the
    // exclusive grab. That matches KWin's behavior.
    return true;
#else
    return false;
#endif
}

QString LayerShellHelper::diagnostics()
{
    const QString plat = platformName();
    const bool wayland = isWayland();
#ifdef EGOBOARD_HAVE_LAYERSHELLQT
    if (wayland) {
        return QStringLiteral("Platform: <b>Wayland</b> (%1) · Layer-shell: <b>available</b> (LayerShellQt) · Quick paste: <b>layer-shell overlay</b> — exclusive keyboard grab, cursor-anchored via margins. Fallback is cursor <code>move()</code> if compositor lacks <code>wlr-layer-shell</code>.").arg(plat.toHtmlEscaped());
    }
    return QStringLiteral("Platform: <b>%1</b> · Layer-shell: <b>n/a</b> (Wayland-only) · Quick paste: cursor-anchored <code>move()</code> popup. On Wayland + KWin it upgrades to layer-shell automatically.").arg(plat.toHtmlEscaped());
#else
    if (wayland) {
        return QStringLiteral("Platform: <b>Wayland</b> (%1) · Layer-shell: <b>not built</b> (LayerShellQt missing at compile time) · Quick paste: cursor <code>move()</code>. Install <code>layer-shell-qt</code> and rebuild to enable overlay.").arg(plat.toHtmlEscaped());
    }
    return QStringLiteral("Platform: <b>%1</b> · Layer-shell: <b>n/a</b> (Wayland-only) · Quick paste: cursor popup (X11/offscreen).").arg(plat.toHtmlEscaped());
#endif
}

void LayerShellHelper::configureForQuickPaste(QWindow *window, QScreen *screen,
                                              QSize desiredSize, QPoint cursorPos)
{
    if (!window || !screen)
        return;
#ifdef EGOBOARD_HAVE_LAYERSHELLQT
    if (!isAvailable())
        return;

    // Compute offset from the screen's geometry origin to the cursor-clamped pos.
    // QuickPasteMenu already clamps `cursorPos` inside availableGeometry, so we
    // just translate to screen-local coordinates for the margin.
    const QRect geom = screen->geometry();
    const int marginLeft = cursorPos.x() - geom.x();
    const int marginTop  = cursorPos.y() - geom.y();

    auto *ls = LayerShellQt::Window::get(window);
    if (!ls)
        return;

    // Anchored to top+left: the surface's top-left corner is offset by margins
    // from the output's origin. exclusiveZone=-1 means "don't reserve space".
    ls->setLayer(LayerShellQt::Window::LayerOverlay);
    ls->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
    ls->setMargins(QMargins(marginLeft, marginTop, 0, 0));
    ls->setExclusiveZone(-1);
    ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    ls->setScope(QStringLiteral("egoboard-quickpaste"));
    ls->setDesiredSize(desiredSize);
    // Use the cursor's screen; don't let compositor pick the active one.
    if (ls->screen() != screen)
        ls->setScreen(screen);
    ls->setWantsToBeOnActiveScreen(false);
    ls->setCloseOnDismissed(false);
#else
    Q_UNUSED(window)
    Q_UNUSED(screen)
    Q_UNUSED(desiredSize)
    Q_UNUSED(cursorPos)
#endif
}
