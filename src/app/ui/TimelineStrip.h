#pragma once
#include "DesignTokens.h"
#include "FilterSpec.h"
#include <QFontMetrics>
#include <QWidget>
class IClipboardStorage;
class QVariantAnimation;

// Thin horizontal histogram above the list: shows entry count per day
// for the current filter (last 14 days). Local-only, no network.
//
// U10: keyboard-navigable (arrows move the day cursor, Enter/Space applies,
// Esc clears) with per-bar accessible names, so the strip is fully usable
// without a mouse or with a screen reader.
class TimelineStrip : public QWidget {
    Q_OBJECT

    // U10 accessible bars live in the .cpp and drive selection/activation.
    friend class TimelineStripAccessible;
    friend class TimelineBarAccessible;

public:
    explicit TimelineStrip(IClipboardStorage *storage, QWidget *parent = nullptr);
    void setFilter(const FilterSpec &filter);
    // The day filter was dropped somewhere else (date preset, "Any time"):
    // stop showing a clicked bar as active.
    void clearSelection();
    // U10 narrow-combo variant: days with entries as combo rows (same caption
    // language as the paint labels), so day filtering stays reachable while
    // the strip is collapsed.
    struct DayOption {
        QString label; // "Today" / "Yest." / "M/d"
        qint64 fromMs = 0;
        qint64 toMs = 0;
        int count = 0;
    };
    QVector<DayOption> dayOptions() const;
    // Drives the strip from the narrow combo with click-identical toggle
    // semantics (unknown or empty days clear the day filter).
    void selectDay(qint64 fromMs);
    // Day start behind a bar index (0 when outside the bins).
    qint64 barDayStart(int index) const;
    // Keyboard day cursor (-1 = none yet) and active filter bar (-1 = none).
    int focusedBar() const { return m_focusedBar; }
    int selectedBar() const { return m_selected; }
    // Height follows the UI font (U5) instead of a fixed pixel value.
    QSize sizeHint() const override
    {
        return QSize(200, DesignTokens::timelineHeightForFont(QFontMetrics(font())));
    }
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
signals:
    void daySelected(qint64 fromMs, qint64 toMs); // emits range for clicked bar, 0/0 = clear
private:
    void recompute();
    // Moves the hover highlight (with the capped transition) to another bar.
    void setHovered(int index);
    // Moves the keyboard cursor and announces it to assistive tech.
    void setFocusedBar(int index);
    // Shared mouse/keyboard/AT activation: empty, outside or re-activated
    // bars clear the day filter, anything else filters by that day.
    void activateBar(int index);
    int barIndexAt(const QPoint &pos) const; // -1 when outside any bar
    QRect barRect(int index) const; // bar paint rect (shared by paint + AT)
    QString barAccessibleName(int index) const; // "12 entries, Monday"
    IClipboardStorage *m_storage = nullptr;
    FilterSpec m_filter;
    struct DayBin { qint64 dayStartMs = 0; int count = 0; };
    QVector<DayBin> m_bins;
    int m_hovered = -1;
    int m_selected = -1;
    int m_focusedBar = -1; // keyboard cursor
    qreal m_hoverStrength = 0.0;
    QVariantAnimation *m_hoverAnimation = nullptr;
    QString m_defaultHint;
};
