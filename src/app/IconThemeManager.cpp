#include "IconThemeManager.h"

#include "IconThemeIndex.h"

#include <QIcon>

void IconThemeManager::apply(const QString &themeId)
{
    // Qt only searches the XDG icon dirs when a platform theme passes them on,
    // so make sure everything IconThemeIndex can list is resolvable through
    // QIcon::fromTheme(). Entries already present keep their precedence.
    QStringList searchPaths = QIcon::themeSearchPaths();
    const QStringList iconDirectories = IconThemeIndex::iconDirectories();
    for (const QString &directory : iconDirectories) {
        if (!searchPaths.contains(directory))
            searchPaths.append(directory);
    }
    QIcon::setThemeSearchPaths(searchPaths);

    // Icons the selected theme does not ship (our own app icon, for one) fall
    // back to the Plasma default instead of disappearing.
    const QString fallback = IconThemeIndex::fallbackId();
    if (!fallback.isEmpty())
        QIcon::setFallbackThemeName(fallback);

    // "system" resolves to whatever Plasma has active; when nothing is
    // configured the Plasma default is used, so the UI cannot end up icon-less.
    const QString resolved = IconThemeIndex::resolvedId(themeId);
    const QString effective = !resolved.isEmpty() ? resolved : fallback;
    if (!effective.isEmpty())
        QIcon::setThemeName(effective);
}
