#pragma once

#include "DashboardStats.h"

#include <QDialog>
#include <QPair>
#include <QString>
#include <QVector>

class IClipboardStorage;
class QLabel;
class QListWidget;
class QScrollArea;
class QWidget;

// P2-A usage dashboard: read-only local aggregates over the existing history
// indexes. Aggregates only — entry text is never rendered; sensitive entries
// are counted with an explicit "content hidden" label, never previewed. All
// charts are custom QWidget/QPainter (no QtCharts dependency, no bundled
// assets): QPalette colors, tr() strings, keyboard-focusable with accessible
// names. Honors "Reduce motion" by construction (no animation in this dialog).
class DashboardBarChart : public QWidget {
    Q_OBJECT
public:
    explicit DashboardBarChart(QWidget *parent = nullptr);

    void setBars(const QVector<QPair<QString, int>> &bars);
    int barCount() const { return m_bars.size(); }
    int barValue(int index) const;
    int focusedBar() const { return m_focusedBar; }
    void setFocusedBar(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRect barRect(int index) const;
    void announceFocus() const;

    QVector<QPair<QString, int>> m_bars;
    int m_focusedBar = -1;
};

class DashboardDialog : public QDialog {
    Q_OBJECT
public:
    explicit DashboardDialog(IClipboardStorage *storage, QWidget *parent = nullptr);

    const DashboardStats &stats() const { return m_stats; }
    // Test seams: the charts and the top-apps list behind their sections.
    DashboardBarChart *dayChart() const { return m_dayChart; }
    DashboardBarChart *typeChart() const { return m_typeChart; }
    DashboardBarChart *sizeChart() const { return m_sizeChart; }
    QListWidget *topAppsList() const { return m_topApps; }

    // Section titles (translated here so tests pin the user-facing language in
    // one place without depending on widget lookup).
    static QString activityTitle();
    static QString typeTitle();
    static QString topAppsTitle();
    static QString sizeTitle();
    static QString sizeBucketLabel(int index);
    static QString typeLabel(int contentType);

private:
    void rebuild();

    IClipboardStorage *m_storage = nullptr;
    DashboardStats m_stats;
    QLabel *m_summary = nullptr;
    QWidget *m_content = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_emptyState = nullptr;
    DashboardBarChart *m_dayChart = nullptr;
    DashboardBarChart *m_typeChart = nullptr;
    DashboardBarChart *m_sizeChart = nullptr;
    QListWidget *m_topApps = nullptr;
};
