#include "TimelineStrip.h"
#include "IClipboardStorage.h"
#include "FilterSpec.h"
#include "StorageManager.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QPainter>
#include <QDateTime>
#include <QMouseEvent>
#include <QToolTip>
#include "SearchEngine.h"

TimelineStrip::TimelineStrip(IClipboardStorage *storage, QWidget *parent)
    : QWidget(parent), m_storage(storage)
{
    setMouseTracking(true);
    setToolTip(tr("Click a bar to filter by day \u2022 click again to clear"));
    recompute();
}

void TimelineStrip::setFilter(const FilterSpec &filter)
{
    m_filter = filter;
    recompute();
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
            qint64 to = from + 86400000 - 1;
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
    const int w = width(), h = height();
    if (m_bins.isEmpty()) return;
    int maxCount = 1;
    for (auto &b: m_bins) maxCount = qMax(maxCount, b.count);
    const int n = m_bins.size();
    const int barW = qMax(4, (w - 16 - (n-1)*4) / n);
    const int totalW = n*barW + (n-1)*4;
    int x0 = (w - totalW)/2;
    const int y0 = 8, barH = h - 20;
    // background
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(rect(), 6, 6);
    for (int i = 0; i < n; ++i) {
        const auto &b = m_bins[i];
        int bh = b.count == 0 ? 2 : qMax(4, barH * b.count / maxCount);
        QRect br(x0 + i*(barW+4), y0 + barH - bh, barW, bh);
        QColor col = palette().color(QPalette::Highlight);
        if (b.count == 0) col.setAlpha(60);
        else col.setAlpha(180);
        if (i == n-1) col.setAlpha(255); // today emphasised
        if (i == m_hovered) col = col.lighter(130);
        p.setBrush(col);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(br, 2, 2);
        // day label
        if (n <= 14) {
            QDate d = QDateTime::fromMSecsSinceEpoch(b.dayStartMs).date();
            QString label = (i == n-1) ? tr("Today") : (i == n-2) ? tr("Yest.") : d.toString(QStringLiteral("M/d"));
            p.setPen(palette().color(QPalette::Mid));
            QFont f = p.font(); f.setPointSize(7); p.setFont(f);
            p.drawText(QRect(br.x()-2, y0+barH+2, barW+4, 10), Qt::AlignCenter, label);
        }
    }
    // count on hover
    if (m_hovered >= 0 && m_hovered < n) {
        p.setPen(palette().color(QPalette::HighlightedText));
        // not needed
    }
}

void TimelineStrip::mousePressEvent(QMouseEvent *event)
{
    const int n = m_bins.size();
    if (n == 0) return;
    const int w = width();
    const int barW = qMax(4, (w - 16 - (n-1)*4) / n);
    const int totalW = n*barW + (n-1)*4;
    int x0 = (w - totalW)/2;
    int x = event->pos().x();
    int idx = (x - x0) / (barW+4);
    if (idx < 0 || idx >= n) { emit daySelected(0,0); return; }
    if (m_bins[idx].count == 0) { emit daySelected(0,0); return; }
    qint64 from = m_bins[idx].dayStartMs;
    qint64 to = from + 86400000 - 1;
    emit daySelected(from, to);
}
