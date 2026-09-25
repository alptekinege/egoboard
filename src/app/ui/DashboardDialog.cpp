#include "DashboardDialog.h"

#include "ContentType.h"
#include "DesignTokens.h"
#include "IClipboardStorage.h"
#include "TextAppearance.h"
#include "UiHelpers.h"

#include <QAccessible>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString shortDayLabel(qint64 dayStartMs, bool isToday, bool isYesterday)
{
    if (isToday)
        return DashboardPanel::tr("Today");
    if (isYesterday)
        return DashboardPanel::tr("Yest.");
    return QDateTime::fromMSecsSinceEpoch(dayStartMs).date().toString(QStringLiteral("M/d"));
}

} // namespace

DashboardBarChart::DashboardBarChart(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus); // Tab reaches the chart, arrows move
    setMinimumHeight(120);
}

void DashboardBarChart::setBars(const QVector<QPair<QString, int>> &bars)
{
    m_bars = bars;
    m_focusedBar = bars.isEmpty() ? -1 : bars.size() - 1;
    update();
}

int DashboardBarChart::barValue(int index) const
{
    if (index < 0 || index >= m_bars.size())
        return 0;
    return m_bars.at(index).second;
}

void DashboardBarChart::setFocusedBar(int index)
{
    if (m_bars.isEmpty())
        return;
    const int clamped = qBound(0, index, m_bars.size() - 1);
    if (clamped == m_focusedBar)
        return;
    m_focusedBar = clamped;
    update();
    announceFocus();
}

void DashboardBarChart::announceFocus() const
{
    if (m_focusedBar < 0 || m_focusedBar >= m_bars.size())
        return;
    QAccessibleEvent focusEvent(const_cast<DashboardBarChart *>(this), QAccessible::Focus);
    QAccessible::updateAccessibility(&focusEvent);
}

QRect DashboardBarChart::barRect(int index) const
{
    const int n = m_bars.size();
    if (index < 0 || index >= n)
        return {};
    const QFontMetrics captions(TextAppearance::withFontPointDelta(font(), -3));
    const DesignTokens::TimelineGeometry geometry =
        DesignTokens::timelineGeometry(size(), n, captions.height());
    int maxCount = 1;
    for (const auto &bar : m_bars)
        maxCount = qMax(maxCount, bar.second);
    const int count = m_bars.at(index).second;
    const int height = count == 0 ? 2 : qMax(4, geometry.barHeight * count / maxCount);
    return {geometry.left + index * geometry.stride, geometry.top + geometry.barHeight - height,
            geometry.barWidth, height};
}

void DashboardBarChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int n = m_bars.size();
    if (n == 0)
        return;

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawRoundedRect(rect(), DesignTokens::RadiusL, DesignTokens::RadiusL);

    const QFont captions = TextAppearance::withFontPointDelta(font(), -3);
    const QFontMetrics captionMetrics(captions);
    const DesignTokens::TimelineGeometry geometry =
        DesignTokens::timelineGeometry(size(), n, captionMetrics.height());

    int maxCount = 1;
    for (const auto &bar : m_bars)
        maxCount = qMax(maxCount, bar.second);

    for (int i = 0; i < n; ++i) {
        const int count = m_bars.at(i).second;
        DesignTokens::TimelineBarState state;
        state.count = count;
        state.today = count == maxCount && count > 0;
        state.hovered = false;
        state.selected = i == m_focusedBar && hasFocus();
        state.hoverStrength = 1.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(DesignTokens::timelineBarColor(palette(), state));
        const QRect bar = barRect(i);
        painter.drawRoundedRect(bar, DesignTokens::RadiusS, DesignTokens::RadiusS);
        if (hasFocus() && i == m_focusedBar) {
            painter.setPen(QPen(DesignTokens::focusRingColor(palette()),
                                DesignTokens::FocusRingWidth));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bar.adjusted(-2, -2, 2, 2), DesignTokens::RadiusL,
                                    DesignTokens::RadiusL);
        }
        const QString label = m_bars.at(i).first;
        const QRect captionRect(bar.x() - 2,
                                geometry.top + geometry.barHeight + DesignTokens::SpaceXs,
                                bar.width() + 4, captionMetrics.height());
        if (!label.isEmpty()
            && captionMetrics.horizontalAdvance(label) <= captionRect.width()) {
            painter.setFont(captions);
            painter.setPen(DesignTokens::timelineCaptionColor(palette(), false));
            painter.drawText(captionRect, Qt::AlignCenter, label);
        }
        // The count above the bar, when it fits without touching neighbours.
        if (count > 0) {
            const QString value = QString::number(count);
            const int valueWidth = captionMetrics.horizontalAdvance(value);
            if (valueWidth <= bar.width() + 4) {
                painter.setFont(captions);
                painter.setPen(palette().color(QPalette::Text));
                painter.drawText(QRect(bar.x() - 2, qMax(0, bar.y() - captionMetrics.height() - 2),
                                       bar.width() + 4, captionMetrics.height()),
                                 Qt::AlignCenter, value);
            }
        }
    }
}

void DashboardBarChart::keyPressEvent(QKeyEvent *event)
{
    const int n = m_bars.size();
    if (n == 0) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (m_focusedBar < 0 || m_focusedBar >= n)
        m_focusedBar = n - 1;
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Up:
        setFocusedBar((m_focusedBar + n - 1) % n);
        event->accept();
        return;
    case Qt::Key_Right:
    case Qt::Key_Down:
        setFocusedBar((m_focusedBar + 1) % n);
        event->accept();
        return;
    case Qt::Key_Home:
        setFocusedBar(0);
        event->accept();
        return;
    case Qt::Key_End:
        setFocusedBar(n - 1);
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void DashboardBarChart::focusInEvent(QFocusEvent *event)
{
    if (m_focusedBar < 0 && !m_bars.isEmpty())
        m_focusedBar = m_bars.size() - 1;
    update();
    QWidget::focusInEvent(event);
}

void DashboardBarChart::mousePressEvent(QMouseEvent *event)
{
    const int index = DesignTokens::timelineBarAt(size(), m_bars.size(), event->pos());
    if (index >= 0)
        setFocusedBar(index);
    QWidget::mousePressEvent(event);
}

QString DashboardPanel::activityTitle()
{
    return tr("Activity — entries per day (last 14 days)");
}

QString DashboardPanel::typeTitle()
{
    return tr("Entries by type");
}

QString DashboardPanel::topAppsTitle()
{
    return tr("Top source apps");
}

QString DashboardPanel::sizeTitle()
{
    return tr("Entries by size");
}

QString DashboardPanel::sizeBucketLabel(int index)
{
    switch (index) {
    case 0:
        return tr("< 1 kB");
    case 1:
        return tr("1–10 kB");
    case 2:
        return tr("10–100 kB");
    default:
        return tr("≥ 100 kB");
    }
}

QString DashboardPanel::typeLabel(int contentType)
{
    switch (static_cast<ContentType>(contentType)) {
    case ContentType::Text:
        return tr("Text");
    case ContentType::RichText:
        return tr("Rich text");
    case ContentType::Image:
        return tr("Images");
    case ContentType::Files:
        return tr("Files");
    }
    return tr("Text");
}

DashboardPanel::DashboardPanel(IClipboardStorage *storage, QWidget *parent)
    : QWidget(parent)
    , m_storage(storage)
{
    setAccessibleName(tr("Usage dashboard"));
    setAccessibleDescription(
        tr("Local-only aggregates over the clipboard history — no entry text is shown."));

    auto *layout = new QVBoxLayout(this);

    m_summary = new QLabel(this);
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    m_summary->setAccessibleName(tr("History summary"));
    layout->addWidget(m_summary);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_content = new QWidget(m_scroll);
    m_scroll->setWidget(m_content);
    layout->addWidget(m_scroll, 1);

    auto *hint = UiHelpers::makeHint(
        tr("Usage dashboard — local-only aggregates; entry text is never shown here. "
           "Sensitive entries are counted, never previewed."),
        this, /*richText=*/false);
    layout->addWidget(hint);

    auto *refresh = new QPushButton(tr("Refresh"), this);
    refresh->setAccessibleName(tr("Refresh dashboard"));
    refresh->setMinimumHeight(DesignTokens::TouchTargetCompact);
    connect(refresh, &QPushButton::clicked, this, &DashboardPanel::refresh);
    layout->addWidget(refresh, 0, Qt::AlignRight);

    m_stats = DashboardStats::collect(m_storage);
    rebuild();
}

void DashboardPanel::refresh()
{
    m_stats = DashboardStats::collect(m_storage);
    rebuild();
}

void DashboardPanel::rebuild()
{
    // Summary line: aggregates only, sensitive counts labeled without content.
    const QString sizeText = UiHelpers::humanSize(m_stats.totalBytes);
    m_summary->setText(
        tr("%1 entries%2 · %3 pinned · %4 sensitive (content hidden) · %5 images · "
           "%6 with OCR text · streak %7")
            .arg(m_stats.totalEntries)
            .arg(sizeText.isEmpty() ? QString() : tr(" (%1)").arg(sizeText))
            .arg(m_stats.pinnedCount)
            .arg(m_stats.sensitiveCount)
            .arg(m_stats.imageCount)
            .arg(m_stats.ocrCount)
            .arg(tr("%n day(s) in a row", nullptr, m_stats.streakDays)));
    m_summary->setAccessibleDescription(m_summary->text());

    // Drop the previous content (charts are rebuilt from the fresh snapshot).
    // The pointers below are re-created below; clearing them first keeps a
    // failed rebuild from leaving dangling test seams behind.
    m_dayChart = nullptr;
    m_typeChart = nullptr;
    m_sizeChart = nullptr;
    m_topApps = nullptr;
    m_emptyState = nullptr;
    if (QLayout *old = m_content->layout()) {
        while (QLayoutItem *item = old->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        delete old;
    }

    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(DesignTokens::SpaceS, DesignTokens::SpaceS,
                                      DesignTokens::SpaceS, DesignTokens::SpaceS);
    contentLayout->setSpacing(DesignTokens::SpaceM);

    if (m_stats.isEmpty()) {
        m_emptyState = UiHelpers::makeEmptyState(
            QStringLiteral("view-statistics"), tr("No entries yet"),
            tr("Copy something and the dashboard will summarize it here — counts by day, "
               "app, type and size."),
            m_content);
        m_emptyState->setAccessibleName(tr("Dashboard empty state"));
        contentLayout->addWidget(m_emptyState);
        contentLayout->addStretch(1);
        return;
    }

    const auto addSection = [&](const QString &title, const QString &description, QWidget *body,
                                const QString &chartName) {
        auto *titleLabel = new QLabel(title, m_content);
        QFont titleFont = titleLabel->font();
        titleFont.setWeight(QFont::DemiBold);
        titleLabel->setFont(titleFont);
        titleLabel->setTextFormat(Qt::PlainText);
        contentLayout->addWidget(titleLabel);
        body->setAccessibleName(chartName);
        body->setAccessibleDescription(description);
        UiHelpers::ensureTouchTarget(body, QStringLiteral("comfortable"));
        contentLayout->addWidget(body);
    };

    // Activity: one bar per day, oldest first.
    {
        QVector<QPair<QString, int>> bars;
        bars.reserve(m_stats.last14Days.size());
        const int n = m_stats.last14Days.size();
        for (int i = 0; i < n; ++i) {
            const DashboardDay &day = m_stats.last14Days.at(i);
            bars.append({shortDayLabel(day.dayStartMs, i == n - 1, i == n - 2), day.count});
        }
        m_dayChart = new DashboardBarChart(m_content);
        m_dayChart->setBars(bars);
        int total = 0;
        for (const auto &bar : bars)
            total += bar.second;
        addSection(activityTitle(),
                   tr("Entries per day for the last 14 days, %1 entries in total.").arg(total),
                   m_dayChart, tr("Activity chart"));
        m_dayChart->setToolTip(tr("%1 entries in the last 14 days").arg(total));
    }

    // Types: fixed Text / Rich text / Images / Files order.
    {
        QVector<QPair<QString, int>> bars;
        for (int type : {int(ContentType::Text), int(ContentType::RichText),
                         int(ContentType::Image), int(ContentType::Files)}) {
            bars.append({typeLabel(type), m_stats.countForType(static_cast<ContentType>(type))});
        }
        m_typeChart = new DashboardBarChart(m_content);
        m_typeChart->setMinimumHeight(110);
        m_typeChart->setBars(bars);
        addSection(typeTitle(), tr("Entry counts by content type."), m_typeChart,
                   tr("Entries by type chart"));
    }

    // Top apps: a list (natively keyboard-navigable) with per-row names.
    {
        auto *label = new QLabel(topAppsTitle(), m_content);
        QFont titleFont = label->font();
        titleFont.setWeight(QFont::DemiBold);
        label->setFont(titleFont);
        label->setTextFormat(Qt::PlainText);
        contentLayout->addWidget(label);
        m_topApps = new QListWidget(m_content);
        m_topApps->setAccessibleName(tr("Top source apps"));
        m_topApps->setAccessibleDescription(
            tr("Source applications ordered by entry count, most first."));
        UiHelpers::styleItemList(m_topApps);
        if (m_stats.topApps.isEmpty()) {
            auto *row = new QListWidgetItem(tr("(no source app recorded)"), m_topApps);
            row->setFlags(row->flags() & ~Qt::ItemIsSelectable);
        } else {
            for (const DashboardTopApp &app : m_stats.topApps) {
                auto *row = new QListWidgetItem(
                    tr("%1 — %n entrie(s)", nullptr, app.count).arg(app.app), m_topApps);
                row->setToolTip(row->text());
            }
        }
        m_topApps->setMinimumHeight(qMax(80, m_topApps->sizeHintForRow(0) * m_topApps->count()
                                                  + 2 * m_topApps->frameWidth()));
        m_topApps->setMaximumHeight(220);
        contentLayout->addWidget(m_topApps);
    }

    // Sizes: four fixed buckets.
    {
        QVector<QPair<QString, int>> bars;
        for (int i = 0; i < 4; ++i)
            bars.append({sizeBucketLabel(i), m_stats.sizeBuckets.value(i, 0)});
        m_sizeChart = new DashboardBarChart(m_content);
        m_sizeChart->setMinimumHeight(110);
        m_sizeChart->setBars(bars);
        addSection(sizeTitle(), tr("Entry counts by stored size."), m_sizeChart,
                   tr("Entries by size chart"));
    }

    contentLayout->addStretch(1);
}

DashboardDialog::DashboardDialog(IClipboardStorage *storage, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Usage dashboard"));
    setAccessibleName(tr("Usage dashboard"));
    setAccessibleDescription(
        tr("Local-only aggregates over the clipboard history — no entry text is shown."));

    auto *layout = new QVBoxLayout(this);
    m_panel = new DashboardPanel(storage, this);
    layout->addWidget(m_panel, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(560, 620);
}

const DashboardStats &DashboardDialog::stats() const
{
    return m_panel->stats();
}

DashboardBarChart *DashboardDialog::dayChart() const
{
    return m_panel->dayChart();
}

DashboardBarChart *DashboardDialog::typeChart() const
{
    return m_panel->typeChart();
}

DashboardBarChart *DashboardDialog::sizeChart() const
{
    return m_panel->sizeChart();
}

QListWidget *DashboardDialog::topAppsList() const
{
    return m_panel->topAppsList();
}
