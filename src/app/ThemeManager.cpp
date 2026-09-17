#include "ThemeManager.h"

#include "ColorSchemeIndex.h"

#include <KColorScheme>
#include <KSharedConfig>

#include <QApplication>
#include <QStyle>

void ThemeManager::apply(const QString &themeId)
{
    const QString schemePath = ColorSchemeIndex::filePath(themeId);

    if (schemePath.isEmpty()) {
        // Unknown id, or "system" while no scheme is configured: hand the
        // palette back to the style rather than keeping a stale custom one.
        QApplication::setPalette(QApplication::style()->standardPalette());
        return;
    }

    // KColorScheme is the reader the KDE platform theme itself uses: it takes
    // the [Colors:Window|View|Button|Selection|Tooltip] groups from the scheme
    // file and derives the remaining shade roles, yielding a complete QPalette.
    // KSharedConfig only parses the file, so the theme asset is never rewritten.
    QApplication::setPalette(
        KColorScheme::createApplicationPalette(KSharedConfig::openConfig(schemePath)));
}
