#pragma once
#include "DesignTokens.h"
#include "FilterSpec.h"
#include <QFontMetrics>
#include <QWidget>
class IClipboardStorage;
class QVariantAnimation;

// Thin horizontal histogram above the list: shows entry count per day
// for the current filter (last 14 days). Local-only, no network.
class TimelineStrip : public QWidget {
    Q_OBJECT
public:
    explicit TimelineStrip(IClipboardStorage *storage, QWidget *parent = nullptr);
    void setFilter(const FilterSpec &filter);
    // The day filter was dropped somewhere else (date preset, "Any time"):
    // stop showing a clicked bar as active.
    void clearSelection();
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
signals:
    void daySelected(qint64 fromMs, qint64 toMs); // emits range for clicked bar, 0/0 = clear
private:
    void recompute();
    // Moves the hover highlight (with the capped transition) to another bar.
    void setHovered(int index);
    int barIndexAt(const QPoint &pos) const; // -1 when outside any bar
    IClipboardStorage *m_storage = nullptr;
    FilterSpec m_filter;
    struct DayBin { qint64 dayStartMs = 0; int count = 0; };
    QVector<DayBin> m_bins;
    int m_hovered = -1;
    int m_selected = -1;
    qreal m_hoverStrength = 0.0;
    QVariantAnimation *m_hoverAnimation = nullptr;
    QString m_defaultHint;
};
