#include "ThemeManager.h"

#include "SettingsManager.h"

#include <QApplication>
#include <QStyle>

namespace {
// Every color role must be set explicitly. Anything left untouched keeps the
// value from whichever palette was active before (e.g. a dark system scheme
// while applying Light), which produced unreadable leftover colors such as
// `palette(mid)` small print. Derived from the Breeze color schemes.
void applyBreezeLight()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xef, 0xf0, 0xf2));
    p.setColor(QPalette::WindowText, QColor(0x23, 0x26, 0x29));
    p.setColor(QPalette::Base, QColor(0xfc, 0xfc, 0xfc));
    p.setColor(QPalette::AlternateBase, QColor(0xf3, 0xf4, 0xf6));
    p.setColor(QPalette::ToolTipBase, QColor(0xfe, 0xfe, 0xfe));
    p.setColor(QPalette::ToolTipText, QColor(0x23, 0x26, 0x29));
    p.setColor(QPalette::Text, QColor(0x23, 0x26, 0x29));
    p.setColor(QPalette::Button, QColor(0xef, 0xf0, 0xf2));
    p.setColor(QPalette::ButtonText, QColor(0x23, 0x26, 0x29));
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Midlight, QColor(0xe6, 0xe7, 0xe9));
    p.setColor(QPalette::Mid, QColor(0x9a, 0xa1, 0xaa)); // small print / borders
    p.setColor(QPalette::Dark, QColor(0x6e, 0x71, 0x75));
    p.setColor(QPalette::Shadow, QColor(0x54, 0x56, 0x5a));
    p.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
    p.setColor(QPalette::HighlightedText, QColor(0xfc, 0xfc, 0xfc));
    p.setColor(QPalette::Link, QColor(0x29, 0x80, 0xd9));
    p.setColor(QPalette::LinkVisited, QColor(0x8e, 0x44, 0xad));
    p.setColor(QPalette::PlaceholderText, QColor(0x76, 0x7c, 0x84));
    const QColor disabledText(0xa0, 0xa4, 0xa8);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0xb8, 0xdc, 0xf2));
    QApplication::setPalette(p);
}

void applyBreezeDark()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2a, 0x2e, 0x32));
    p.setColor(QPalette::WindowText, QColor(0xcf, 0xd7, 0xe1));
    p.setColor(QPalette::Base, QColor(0x1b, 0x1e, 0x21));
    p.setColor(QPalette::AlternateBase, QColor(0x25, 0x28, 0x2c));
    p.setColor(QPalette::ToolTipBase, QColor(0x1f, 0x22, 0x25));
    p.setColor(QPalette::ToolTipText, QColor(0xcf, 0xd7, 0xe1));
    p.setColor(QPalette::Text, QColor(0xcf, 0xd7, 0xe1));
    p.setColor(QPalette::Button, QColor(0x31, 0x35, 0x3a));
    p.setColor(QPalette::ButtonText, QColor(0xcf, 0xd7, 0xe1));
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x45, 0x47, 0x4b));
    p.setColor(QPalette::Midlight, QColor(0x36, 0x39, 0x3e));
    // Small print (`color: palette(mid)`) must stay readable on the dark
    // window color — light gray-blue, ~4.6:1 contrast on #2a2e32.
    p.setColor(QPalette::Mid, QColor(0x8f, 0x96, 0xa3));
    p.setColor(QPalette::Dark, QColor(0x14, 0x16, 0x19));
    p.setColor(QPalette::Shadow, QColor(0x0f, 0x11, 0x13));
    p.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
    p.setColor(QPalette::HighlightedText, QColor(0xfc, 0xfc, 0xfc));
    p.setColor(QPalette::Link, QColor(0x1d, 0x99, 0xf3));
    p.setColor(QPalette::LinkVisited, QColor(0x9b, 0x59, 0xb6));
    p.setColor(QPalette::PlaceholderText, QColor(0x8f, 0x96, 0xa3));
    const QColor disabledText(0x6e, 0x75, 0x80);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x2c, 0x51, 0x66));
    QApplication::setPalette(p);
}
} // namespace

void ThemeManager::apply(const QString &theme, SettingsManager *settings)
{
    Q_UNUSED(settings)

    if (theme == QLatin1String("light")) {
        applyBreezeLight();
        return;
    }
    if (theme == QLatin1String("dark")) {
        applyBreezeDark();
        return;
    }
    // "system": restore the active style's own palette (complete, consistent).
    QApplication::setPalette(QApplication::style()->standardPalette());
}
