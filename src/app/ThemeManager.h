#pragma once

#include <QObject>

// Installs a theme into the application palette.
//
// No theme is defined in code: the palette is always built from an installed
// KDE *.colors file (see ColorSchemeIndex), which keeps Egoboard in step with
// the user's Plasma color scheme and makes newly installed schemes available
// without a code change. Scheme files are only ever read, never written.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    // themeId: "system", a legacy "light"/"dark" preset, or the id of an
    // installed scheme. Ids that resolve to no file restore the style's own
    // palette instead of leaving a stale custom one behind.
    static void apply(const QString &themeId);

private:
    ThemeManager() = delete;
};
