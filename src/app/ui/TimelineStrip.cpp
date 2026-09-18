#include "TimelineStrip.h"
#include "DesignTokens.h"
#include "IClipboardStorage.h"
#include "FilterSpec.h"
#include "StorageManager.h"
#include "TextAppearance.h"
#include "UiHelpers.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QPainter>
#include <QDateTime>
#include <QMouseEvent>
#include <QVariantAnimation>
#include "SearchEngine.h"

namespace {
constexpr qint64 kDayMs = 86400000;

// Day captions are drawn three points below the UI font (and follow the
// "Text size" setting with it) instead of at a fixed point size.
QFont captionFont(const QFont &uiFont)
{
    return TextAppearance::withFontPointDelta(uiFont, -3);
}
} // namespace

TimelineStrip::TimelineStrip(IClipboardStorage *storage, QWidget *parent)
    : QWidget(parent), m_storage(storage)
{
    setMouseTracking(true);
    m_defaultHint = tr("Click a bar to filter by day \u2022 click again to clear");
    setToolTip(m_defaultHint);

    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(DesignTokens::MotionDurationMs);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                m_hoverStrength = value.toReal();
                update();
            });
    recompute();
}

void TimelineStrip::setFilter(const FilterSpec &filter)
{
    m_filter = filter;
    recompute();
    update();
}

void TimelineStrip::clearSelection()
{
    if (m_selected == -1)
        return;
    m_selected = -1;
    update();
}

void TimelineStrip::recompute()
{
    m_bins.clear();
    if (!m_storage) return;
    // Build 14 bins: today back to 13 days ago
    const QDate today = QDate::currentDate();
    m_bins.reserve(14);
    for (int i = 13; i >= 0; --i) {
        DayBin b;
        QDate d = today.addDays(-i);
        b.dayStartMs = QDateTime(d, QTime(0,0)).toMSecsSinceEpoch();
        b.count = 0;
        m_bins.append(b);
    }
    // Fetch counts: use fetchAll (already filtered) then bin, but avoid loading full for large DB
    // Do lightweight count per day via direct SQL if storage is StorageManager
    if (auto *sm = qobject_cast<StorageManager*>(m_storage)) {
        QSqlDatabase db = sm->database();
        if (!db.isOpen()) return;
        // Count per day using provided filter's other constraints (type, app, search)
        // For simplicity we ignore complex filters here and just count overall distribution
        // within current filter's search/type/app but not date range (so histogram stays stable).
        // Build WHERE without date.
        QStringList where;
        QList<QVariant> binds;
        if (!m_filter.searchText.isEmpty()) {
            where << QStringLiteral("(preview LIKE ? ESCAPE '\\' OR text_data LIKE ? ESCAPE '\\')");
            const QString needle = QStringLiteral("%") + SearchEngine::likeEscape(m_filter.searchText) + QStringLiteral("%");
            binds << needle << needle;
        }
        if (m_filter.contentType >= 0) { where << QStringLiteral("content_type = ?"); binds << m_filter.contentType; }
        if (!m_filter.sourceApp.isEmpty()) { where << QStringLiteral("source_app = ?"); binds << m_filter.sourceApp; }
        if (m_filter.pinnedOnly) where << QStringLiteral("pinned = 1");
        if (m_filter.groupId.has_value()) where << QStringLiteral("id IN (SELECT entry_id FROM entry_groups WHERE group_id = ?)") , binds << m_filter.groupId.value();

        for (int i = 0; i < m_bins.size(); ++i) {
            qint64 from = m_bins[i].dayStartMs;
            qint64 to = from + kDayMs - 1;
            QString sql = QStringLiteral("SELECT COUNT(*) FROM entries");
            QStringList w2 = where;
            w2 << QStringLiteral("timestamp_ms >= ? AND timestamp_ms <= ?");
            if (!w2.isEmpty()) sql += QStringLiteral(" WHERE ") + w2.join(QStringLiteral(" AND "));
            QSqlQuery q(db);
            q.prepare(sql);
            int idx = 0;
            for (auto &b: binds) q.bindValue(idx++, b);
            q.bindValue(idx++, from);
            q.bindValue(idx++, to);
            if (q.exec() && q.next()) m_bins[i].count = q.value(0).toInt();
        }
    } else {
        // fallback: bin from fetched page
        auto all = m_storage->fetchPage(m_filter, {}, 10000);
        for (auto &r: all) {
            QDate d = QDateTime::fromMSecsSinceEpoch(r.timestamp).date();
            for (int i = 0; i < m_bins.size(); ++i) {
                if (QDateTime::fromMSecsSinceEpoch(m_bins[i].dayStartMs).date() == d) { m_bins[i].count++; break; }
            }
        }
    }
}

void TimelineStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int n = m_bins.size();
    if (n == 0) return;

    int maxCount = 1;
    for (const auto &bin : m_bins) maxCount = qMax(maxCount, bin.count);

    const QFont captions = captionFont(font());
    const QFontMetrics captionMetrics(captions);
    const DesignTokens::TimelineGeometry geometry =
        DesignTokens::timelineGeometry(size(), n, captionMetrics.height());

    // background
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(rect(), DesignTokens::RadiusL, DesignTokens::RadiusL);

    for (int i = 0; i < n; ++i) {
        const auto &b = m_bins[i];

        DesignTokens::TimelineBarState state;
        state.count = b.count;
        state.today = i == n - 1;
        state.hovered = i == m_hovered;
        state.selected = i == m_selected;
        state.hoverStrength = m_hoverStrength;

        const int barHeight =
            b.count == 0 ? 2 : qMax(4, geometry.barHeight * b.count / maxCount);
        const QRect bar(geometry.left + i * geometry.stride,
                        geometry.top + geometry.barHeight - barHeight, geometry.barWidth,
                        barHeight);
        p.setPen(Qt::NoPen);
        p.setBrush(DesignTokens::timelineBarColor(palette(), state));
        p.drawRoundedRect(bar, DesignTokens::RadiusS, DesignTokens::RadiusS);
        if (state.selected) {
            // The clicked bar stays marked while the list is filtered by it.
            QColor outline = palette().color(QPalette::WindowText);
            outline.setAlpha(90);
            p.setPen(QPen(outline, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(bar.adjusted(-1, -1, 1, 1), DesignTokens::RadiusS,
                              DesignTokens::RadiusS);
        }

        // Day label, in the same font the rest of the UI uses (one step below).
        // A caption that cannot fit its own bar is dropped instead of running
        // into its neighbour (only happens on a very narrow strip).
        if (n <= 14) {
            QDate d = QDateTime::fromMSecsSinceEpoch(b.dayStartMs).date();
            QString label = (i == n-1) ? tr("Today") : (i == n-2) ? tr("Yest.") : d.toString(QStringLiteral("M/d"));
            const QRect captionRect(bar.x() - 2,
                                    geometry.top + geometry.barHeight + DesignTokens::SpaceXs,
                                    bar.width() + 4, captionMetrics.height());
            if (captionMetrics.horizontalAdvance(label) <= captionRect.width()) {
                QFont labelFont = captions;
                labelFont.setBold(state.selected);
                p.setFont(labelFont);
                p.setPen(DesignTokens::timelineCaptionColor(palette(), state.selected));
                p.drawText(captionRect, Qt::AlignCenter, label);
            }
        }
    }
}

int TimelineStrip::barIndexAt(const QPoint &pos) const
{
    return DesignTokens::timelineBarAt(size(), m_bins.size(), pos);
}

void TimelineStrip::mousePressEvent(QMouseEvent *event)
{
    const int idx = barIndexAt(event->pos());
    // Clicking an empty bar, outside the strip, or the bar that is already
    // active clears the day filter.
    if (idx < 0 || idx >= m_bins.size() || m_bins[idx].count == 0 || idx == m_selected) {
        clearSelection();
        emit daySelected(0, 0);
        return;
    }
    m_selected = idx;
    update();
    const qint64 from = m_bins[idx].dayStartMs;
    emit daySelected(from, from + kDayMs - 1);
}

void TimelineStrip::mouseMoveEvent(QMouseEvent *event)
{
    const int idx = barIndexAt(event->pos());
    if (idx != m_hovered) {
        setHovered(idx);
        if (idx >= 0) {
            const QDate day = QDateTime::fromMSecsSinceEpoch(m_bins[idx].dayStartMs).date();
            setToolTip(tr("%1 — %n entrie(s)", nullptr, m_bins[idx].count)
                           .arg(day.toString(Qt::ISODate)));
        } else {
            setToolTip(m_defaultHint);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void TimelineStrip::leaveEvent(QEvent *event)
{
    setHovered(-1);
    setToolTip(m_defaultHint);
    QWidget::leaveEvent(event);
}

void TimelineStrip::setHovered(int index)
{
    if (index == m_hovered)
        return;
    m_hovered = index;
    const qreal target = index >= 0 ? 1.0 : 0.0;
    if (UiHelpers::reduceMotion()) {
        m_hoverAnimation->stop();
        m_hoverStrength = target;
        update();
        return;
    }
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverStrength);
    m_hoverAnimation->setEndValue(target);
    m_hoverAnimation->start();
}
