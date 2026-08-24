#include "ThemeManager.h"

#include "SettingsManager.h"

#include <QApplication>
#include <QStyle>

void ThemeManager::apply(const QString &theme, SettingsManager *settings)
{
    Q_UNUSED(settings)

    if (theme == QLatin1String("light")) {
        QPalette p;
        p.setColor(QPalette::Window, QColor(0xef, 0xf0, 0xf2));
        p.setColor(QPalette::WindowText, QColor(0x23, 0x26, 0x31));
        p.setColor(QPalette::Base, QColor(0xfc, 0xfc, 0xfc));
        p.setColor(QPalette::AlternateBase, QColor(0xf3, 0xf4, 0xf6));
        p.setColor(QPalette::ToolTipBase, QColor(0xfe, 0xfe, 0xfe));
        p.setColor(QPalette::ToolTipText, QColor(0x23, 0x26, 0x31));
        p.setColor(QPalette::Text, QColor(0x23, 0x26, 0x31));
        p.setColor(QPalette::Button, QColor(0xef, 0xf0, 0xf2));
        p.setColor(QPalette::ButtonText, QColor(0x23, 0x26, 0x31));
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(0x29, 0x80, 0xd9));
        p.setColor(QPalette::PlaceholderText, QColor(0x78, 0x7c, 0x84));
        QApplication::setPalette(p);
        return;
    }
    if (theme == QLatin1String("dark")) {
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
        p.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(0x1d, 0x99, 0xf3));
        p.setColor(QPalette::PlaceholderText, QColor(0x8f, 0x96, 0xa3));
        // Disabled roles: muted variants
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x76, 0x7c, 0x85));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x76, 0x7c, 0x85));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x76, 0x7c, 0x85));
        QApplication::setPalette(p);
        return;
    }
    // "system": restore the active style's own palette (Breeze/Fusion).
    QApplication::setPalette(QApplication::style()->standardPalette());
}
