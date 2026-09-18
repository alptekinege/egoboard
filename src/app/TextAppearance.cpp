#include "TextAppearance.h"

#include <QtGlobal>

#include <cmath>

namespace {
// WCAG 2.1 relative luminance.
qreal relativeLuminance(const QColor &color)
{
    const auto linear = [](qreal channel) {
        return channel <= 0.03928 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF())
        + 0.0722 * linear(color.blueF());
}

// Text is drawn on more than one surface (list rows sit on Base, labels on
// Window), so a color has to hold up on each of them.
QColor readableOn(const QColor &color, const QList<QColor> &surfaces, qreal minRatio)
{
    QColor result = color;
    // Two passes: pushing away from one surface must not drop the color below
    // the floor on another (only relevant for schemes whose surfaces differ a lot).
    for (int pass = 0; pass < 2; ++pass) {
        for (const QColor &surface : surfaces) {
            if (surface.isValid())
                result = TextAppearance::ensureContrast(result, surface, minRatio);
        }
    }
    return result;
}

QColor roleColor(const QPalette &palette, QPalette::ColorRole role)
{
    return palette.color(QPalette::Active, role);
}
} // namespace

qreal TextAppearance::contrastRatio(const QColor &a, const QColor &b)
{
    if (!a.isValid() || !b.isValid())
        return 1.0;
    const qreal first = relativeLuminance(a);
    const qreal second = relativeLuminance(b);
    const qreal lighter = qMax(first, second);
    const qreal darker = qMin(first, second);
    return (lighter + 0.05) / (darker + 0.05);
}

QColor TextAppearance::ensureContrast(const QColor &color, const QColor &background, qreal minRatio)
{
    if (!color.isValid() || !background.isValid() || minRatio <= 1.0)
        return color;
    if (contrastRatio(color, background) >= minRatio)
        return color;

    // Push toward whichever end of the scale contrasts better with the
    // background, in small steps, so the theme's own hue is kept as long as
    // possible and only the lightness moves.
    const QColor target = contrastRatio(Qt::black, background) >= contrastRatio(Qt::white, background)
        ? QColor(Qt::black)
        : QColor(Qt::white);

    QColor result = color;
    constexpr int kSteps = 100;
    for (int step = 1; step <= kSteps; ++step) {
        const qreal t = qreal(step) / kSteps;
        result = QColor::fromRgbF(color.redF() + (target.redF() - color.redF()) * t,
                                  color.greenF() + (target.greenF() - color.greenF()) * t,
                                  color.blueF() + (target.blueF() - color.blueF()) * t);
        // The floor is measured on the 8-bit color that actually gets painted,
        // not on the intermediate float, so "readable" means readable on screen.
        result = QColor::fromRgb(result.rgb());
        if (contrastRatio(result, background) >= minRatio)
            break;
    }
    result.setAlpha(color.alpha());
    return result;
}

QPalette TextAppearance::applyOverrides(const QPalette &palette, const Overrides &overrides)
{
    QPalette result = palette;
    const QColor window = roleColor(palette, QPalette::Window);
    const QColor base = roleColor(palette, QPalette::Base);
    const QColor alternateBase = roleColor(palette, QPalette::AlternateBase);
    const QColor button = roleColor(palette, QPalette::Button);

    // Primary text: the user's color, or the scheme's own.
    const bool useCustomText = overrides.customText && overrides.textColor.isValid();
    const auto applyText = [&](QPalette::ColorRole role, const QList<QColor> &surfaces) {
        const QColor source = useCustomText ? overrides.textColor : roleColor(palette, role);
        result.setColor(role, readableOn(source, surfaces, kTextContrastRatio));
    };
    applyText(QPalette::Text, {base, alternateBase});
    applyText(QPalette::WindowText, {window});
    applyText(QPalette::ButtonText, {button});

    // Secondary text: hints, timestamps, source apps, placeholders. Kept dimmer
    // than the main text, but never below the readability floor.
    const bool useCustomDim = overrides.customDimText && overrides.dimTextColor.isValid();
    const auto applyDim = [&](QPalette::ColorRole role, const QList<QColor> &surfaces) {
        const QColor source = useCustomDim ? overrides.dimTextColor : roleColor(palette, role);
        result.setColor(role, readableOn(source, surfaces, kDimTextContrastRatio));
    };
    applyDim(QPalette::Mid, {window, base, alternateBase});
    applyDim(QPalette::PlaceholderText, {base, alternateBase});
    return result;
}

QFont TextAppearance::withFontPointDelta(const QFont &font, int pointDelta)
{
    if (pointDelta == 0 || font.pointSizeF() <= 0)
        return font;
    QFont result = font;
    result.setPointSizeF(qMax(1.0, font.pointSizeF() + pointDelta));
    return result;
}
