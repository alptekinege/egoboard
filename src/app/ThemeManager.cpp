#include "ThemeManager.h"

#include "ColorSchemeIndex.h"

#include <KColorScheme>
#include <KSharedConfig>

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyle>

namespace {
// The platform's UI font, captured before any delta is applied, so repeated
// applies cannot compound into ever-larger text.
QFont baseApplicationFont()
{
    static const QFont base = QApplication::font();
    return base;
}

QPalette schemePalette(const QString &themeId)
{
    const QString schemePath = ColorSchemeIndex::filePath(themeId);
    if (schemePath.isEmpty()) {
        // Unknown id, or "system" while no scheme is configured: hand the
        // palette back to the style rather than keeping a stale custom one.
        return QApplication::style()->standardPalette();
    }

    // KColorScheme is the reader the KDE platform theme itself uses: it takes
    // the [Colors:Window|View|Button|Selection|Tooltip] groups from the scheme
    // file and derives the remaining shade roles, yielding a complete QPalette.
    // KSharedConfig only parses the file, so the theme asset is never rewritten.
    return KColorScheme::createApplicationPalette(KSharedConfig::openConfig(schemePath));
}
} // namespace

void ThemeManager::apply(const QString &themeId, const TextAppearance::Overrides &overrides)
{
    // The scheme's palette is only a starting point: TextAppearance replaces the
    // text roles with readable ones (and the user's colors, when set).
    QApplication::setPalette(TextAppearance::applyOverrides(schemePalette(themeId), overrides));

    const QFont font =
        TextAppearance::withFontPointDelta(baseApplicationFont(), overrides.fontPointDelta);
    if (font != QApplication::font())
        QApplication::setFont(font);
}
