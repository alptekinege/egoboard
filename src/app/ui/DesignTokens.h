#pragma once

#include <QColor>
#include <QFontMetrics>
#include <QPalette>
#include <QPoint>
#include <QSize>
#include <QString>

// One place for the pixel values and the palette-derived colors the UI paints
// with, so a tuning pass touches this header instead of every widget.
//
// Pure values and color math (QtGui only), which keeps the metrics and the
// contrast rules unit-testable without a window: the numbers stay literals in
// the widgets' call sites, only their home changes.
namespace DesignTokens {

// Spacing scale.
inline constexpr int SpaceXs = 4;
inline constexpr int SpaceS = 6;
inline constexpr int SpaceM = 8;
inline constexpr int SpaceL = 12;

// Corner radii.
inline constexpr int RadiusS = 3;
inline constexpr int RadiusM = 4;
inline constexpr int RadiusL = 6;

// Icon sizes.
inline constexpr int IconS = 16;
inline constexpr int IconM = 18;
inline constexpr int IconL = 22;

// Alpha (0-255) of the fills derived from the palette's selection color.
inline constexpr int SearchHighlightAlpha = 80; // search match on an idle row
inline constexpr int SearchHighlightSelectedAlpha = 120; // ... on the selected row
inline constexpr int HoverAlpha = 40; // mouse-over wash on a row
inline constexpr int DropTargetAlpha = 90; // row under a drag cursor
inline constexpr int TimelineIdleAlpha = 60; // day with no entries
inline constexpr int TimelineBarAlpha = 180; // day with entries

// Animation length. Motion is capped and skippable (Settings ▸ Appearance ▸
// "Reduce motion"), so this is the only duration in the tree.
inline constexpr int MotionDurationMs = 120;

// List density (SettingsManager::listDensity()) → row padding.
inline constexpr int RowPaddingCompact = 4;
inline constexpr int RowPaddingComfortable = 8;
inline constexpr int RowPaddingSpacious = 12;

inline int rowPaddingForDensity(const QString &density)
{
    if (density == QLatin1String("compact"))
        return RowPaddingCompact;
    if (density == QLatin1String("spacious"))
        return RowPaddingSpacious;
    return RowPaddingComfortable;
}

// Geometry of one history row: shared by the delegate that paints it, the
// settings preview that imitates it and the tests that pin it.
struct RowMetrics {
    int padding = RowPaddingComfortable; // vertical breathing room (density)
    int margin = SpaceM; // horizontal inset of the row content
    int iconSize = IconL; // content-type icon
    int badgeSize = IconS; // pin / sensitive badge
    int dotDiameter = 8; // group color dot
    int dotSpacing = SpaceXs; // gap between two dots

    // Group dots are spaced, never overlapped: the dot plus the gap is the
    // step from one dot to the next (recorded decision, kept in one place).
    int dotAdvance() const { return dotDiameter + dotSpacing; }

    // Left edge of the text block for a row starting at rowLeft.
    int contentLeft(int rowLeft) const { return rowLeft + margin + iconSize + margin; }

    QSize sizeHint(const QFontMetrics &metrics, int width) const
    {
        return QSize(width, metrics.height() * 2 + 2 * padding + SpaceXs);
    }
};

inline RowMetrics rowMetrics(int rowPadding)
{
    RowMetrics metrics;
    metrics.padding = rowPadding;
    return metrics;
}

// --- row colors --------------------------------------------------------------

inline QColor previewTextColor(const QPalette &palette, bool selected, bool enabled)
{
    return palette.color(enabled ? QPalette::Normal : QPalette::Disabled,
                         selected ? QPalette::HighlightedText : QPalette::Text);
}

inline QColor metaTextColor(const QPalette &palette, bool selected, bool enabled)
{
    // Secondary line: the palette's subdued role, which TextAppearance keeps
    // above a readability floor on every surface it is drawn on.
    return palette.color(enabled ? QPalette::Normal : QPalette::Disabled,
                         selected ? QPalette::HighlightedText : QPalette::Mid);
}

inline QColor searchHighlightFill(const QPalette &palette, bool selected)
{
    QColor fill = palette.color(QPalette::Highlight);
    fill.setAlpha(selected ? SearchHighlightSelectedAlpha : SearchHighlightAlpha);
    return fill;
}

inline QColor hoverBackground(const QPalette &palette)
{
    QColor color = palette.color(QPalette::Highlight);
    color.setAlpha(HoverAlpha);
    return color;
}

// --- timeline ----------------------------------------------------------------

struct TimelineBarState {
    int count = 0;
    bool today = false;
    bool hovered = false;
    bool selected = false; // the day the list is currently filtered by
    qreal hoverStrength = 1.0; // 0..1 while the hover transition runs
};

inline QColor timelineBarColor(const QPalette &palette, const TimelineBarState &state)
{
    QColor color = palette.color(QPalette::Highlight);
    int alpha = state.count == 0 ? TimelineIdleAlpha : TimelineBarAlpha;
    if (state.today || state.selected)
        alpha = 255;
    color.setAlpha(alpha);
    if (state.hovered)
        color = color.lighter(100 + qRound(30.0 * qBound(0.0, state.hoverStrength, 1.0)));
    return color;
}

inline QColor timelineCaptionColor(const QPalette &palette, bool selected)
{
    return palette.color(selected ? QPalette::Text : QPalette::Mid);
}

// Geometry of the 14-day histogram: shared by paintEvent and barIndexAt, so the
// bar a click lands on is always the bar that was drawn there.
struct TimelineGeometry {
    int left = 0; // x of the first bar
    int barWidth = 0;
    int stride = 0; // bar + gap
    int top = 0;
    int barHeight = 0;
    int captionHeight = 0;
};

inline constexpr int TimelineBarGap = SpaceXs;
inline constexpr int TimelineSideMargin = SpaceM;
inline constexpr int TimelineMinBarWidth = 4;

inline TimelineGeometry timelineGeometry(const QSize &size, int barCount, int captionHeight)
{
    TimelineGeometry geometry;
    geometry.captionHeight = captionHeight;
    if (barCount <= 0)
        return geometry;
    geometry.barWidth = qMax(TimelineMinBarWidth,
                             (size.width() - 2 * TimelineSideMargin
                              - (barCount - 1) * TimelineBarGap)
                                 / barCount);
    geometry.stride = geometry.barWidth + TimelineBarGap;
    const int total = barCount * geometry.barWidth + (barCount - 1) * TimelineBarGap;
    geometry.left = (size.width() - total) / 2;
    geometry.top = SpaceM;
    geometry.barHeight = size.height() - geometry.top - captionHeight - SpaceXs;
    return geometry;
}

inline int timelineBarAt(const QSize &size, int barCount, const QPoint &pos)
{
    // Hit-testing only needs the horizontal layout, so the caption strip can be
    // left out of the calculation.
    const TimelineGeometry geometry = timelineGeometry(size, barCount, 0);
    if (barCount <= 0 || geometry.stride <= 0 || pos.x() < geometry.left)
        return -1; // left of the first bar (integer division would round it up to 0)
    const int index = (pos.x() - geometry.left) / geometry.stride;
    return (index < 0 || index >= barCount) ? -1 : index;
}

} // namespace DesignTokens
