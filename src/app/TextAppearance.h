#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

// Keeps the app's text readable whatever color scheme is loaded.
//
// KDE color schemes are free to define low-contrast foreground/background pairs
// (and the "subdued" roles used for secondary text are often barely visible), so
// Egoboard does not trust them blindly: every text role is checked against the
// surfaces it is painted on and pushed just far enough to stay readable. The
// user can also override the two text colors and the font size.
//
// Pure value/math helpers (QtGui only), so the rules stay unit-testable
// headlessly and ThemeManager stays a thin "read the scheme, install it" layer.
namespace TextAppearance {

// WCAG 2.1 minimum contrast ratios: 4.5:1 for body text, 3:1 for secondary
// text, which is allowed to stay deliberately dimmer than the main text.
constexpr qreal kTextContrastRatio = 4.5;
constexpr qreal kDimTextContrastRatio = 3.0;

struct Overrides {
    int fontPointDelta = 0; // 0 = the platform UI font size
    bool customText = false;
    QColor textColor; // used when customText is set
    bool customDimText = false;
    QColor dimTextColor; // secondary/small print, used when customDimText is set

    bool operator==(const Overrides &other) const = default;
};

// WCAG 2.1 contrast ratio between two colors, 1.0 (identical) .. 21.0.
qreal contrastRatio(const QColor &a, const QColor &b);

// The color, moved toward black or white until it reaches minRatio against the
// background. Colors that already pass are returned untouched.
QColor ensureContrast(const QColor &color, const QColor &background, qreal minRatio);

// Applies the overrides and the contrast floor to a scheme palette: primary text
// (Text/WindowText/ButtonText) and the subdued roles the UI uses for hints,
// timestamps and placeholders (Mid/PlaceholderText). The Disabled group is left
// alone - dimmed is what "disabled" is supposed to look like.
QPalette applyOverrides(const QPalette &palette, const Overrides &overrides);

// Point-size delta applied to a font; fonts sized in pixels (pointSizeF() < 0)
// are returned unchanged.
QFont withFontPointDelta(const QFont &font, int pointDelta);

} // namespace TextAppearance
