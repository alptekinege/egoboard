#pragma once
#include "FilterSpec.h"
#include <QWidget>
class IClipboardStorage;

// Thin horizontal histogram above the list: shows entry count per day
// for the current filter (last 14 days). Local-only, no network.
class TimelineStrip : public QWidget {
    Q_OBJECT
public:
    explicit TimelineStrip(IClipboardStorage *storage, QWidget *parent = nullptr);
    void setFilter(const FilterSpec &filter);
    QSize sizeHint() const override { return QSize(200, 48); }
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
signals:
    void daySelected(qint64 fromMs, qint64 toMs); // emits range for clicked bar, 0/0 = clear
private:
    void recompute();
    int barIndexAt(const QPoint &pos) const; // -1 when outside any bar
    IClipboardStorage *m_storage = nullptr;
    FilterSpec m_filter;
    struct DayBin { qint64 dayStartMs = 0; int count = 0; };
    QVector<DayBin> m_bins;
    int m_hovered = -1;
    QString m_defaultHint;
};
