#include "TimelineStrip.h"
#include "DesignTokens.h"
#include "IClipboardStorage.h"
#include "FilterSpec.h"
#include "StorageManager.h"
#include "TextAppearance.h"
#include "UiHelpers.h"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QPainter>
#include <QDateTime>
#include <QKeyEvent>
#include <QLocale>
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

// --- U10 accessible bars ------------------------------------------------------
// One lightweight child interface per day bar (name + press action), so screen
// readers announce "12 entries, Monday" instead of one opaque strip.
//
// Identity note: Qt files cached interfaces under their object(), so every
// bar owns a tiny proxy QObject — sharing the strip would overwrite the
// parent's own cache entry with the last-enumerated bar. Proxies are owned by
// the parent interface (plain news, no Qt parent); bars themselves are owned
// by Qt's cache and die with their proxy, so the parent only clears its map.
class TimelineBarAccessible : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    TimelineBarAccessible(QPointer<TimelineStrip> strip, QObject *identity, int index)
        : m_strip(strip)
        , m_identity(identity)
        , m_index(index)
    {
    }

    bool isValid() const override
    {
        return !m_strip.isNull() && m_identity && m_index >= 0
            && m_index < m_strip->m_bins.size();
    }
    QObject *object() const override { return m_identity; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; } // leaf
    QAccessibleInterface *parent() const override
    {
        return m_strip.isNull() ? nullptr
                                : QAccessible::queryAccessibleInterface(m_strip.data());
    }
    QAccessibleInterface *child(int) const override { return nullptr; } // leaf
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text t) const override
    {
        if (!isValid())
            return {};
        if (t == QAccessible::Name)
            return m_strip->barAccessibleName(m_index);
        if (t == QAccessible::Description)
            return TimelineStrip::tr("Press Enter to filter by this day.");
        return {};
    }
    void setText(QAccessible::Text, const QString &) override {}
    QRect rect() const override
    {
        if (!isValid())
            return {};
        const QRect local = m_strip->barRect(m_index);
        if (local.isNull())
            return {};
        return {m_strip->mapToGlobal(local.topLeft()), local.size()};
    }
    QAccessible::Role role() const override { return QAccessible::ListItem; }
    QAccessible::State state() const override
    {
        QAccessible::State state;
        state.focusable = true;
        state.selectable = true;
        if (!isValid())
            return state;
        if (m_index == m_strip->m_selected)
            state.selected = true;
        if (m_index == m_strip->m_focusedBar && m_strip->hasFocus())
            state.focused = true;
        return state;
    }
    void *interface_cast(QAccessible::InterfaceType type) override
    {
        if (type == QAccessible::ActionInterface)
            return static_cast<QAccessibleActionInterface *>(this);
        return nullptr;
    }
    QStringList actionNames() const override
    {
        return {QAccessibleActionInterface::pressAction()};
    }
    void doAction(const QString &actionName) override
    {
        if (actionName == QAccessibleActionInterface::pressAction() && isValid())
            m_strip->activateBar(m_index);
    }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }
    int index() const { return m_index; }

private:
    QPointer<TimelineStrip> m_strip;
    QObject *m_identity = nullptr; // owned by the parent interface, not by Qt
    int m_index = -1;
};

class TimelineStripAccessible : public QAccessibleWidget {
public:
    explicit TimelineStripAccessible(TimelineStrip *strip)
        : QAccessibleWidget(strip, QAccessible::List)
    {
    }
    ~TimelineStripAccessible() override
    {
        // Bars die with their proxies through Qt's cache hooks; dropping the
        // map here must not delete them again.
        qDeleteAll(m_proxies);
        m_bars.clear();
    }
    int childCount() const override
    {
        const auto *strip = static_cast<TimelineStrip *>(object());
        return strip ? strip->m_bins.size() : 0;
    }
    QAccessibleInterface *child(int index) const override
    {
        auto *strip = static_cast<TimelineStrip *>(object());
        if (!strip || index < 0 || index >= strip->m_bins.size())
            return nullptr;
        auto it = m_bars.constFind(index);
        if (it == m_bars.constEnd()) {
            auto *identity = new QObject();
            m_proxies.append(identity);
            auto *bar = new TimelineBarAccessible(strip, identity, index);
            m_bars.insert(index, bar);
            return bar;
        }
        return it.value();
    }
    int indexOfChild(const QAccessibleInterface *child) const override
    {
        for (auto it = m_bars.constBegin(); it != m_bars.constEnd(); ++it) {
            if (it.value() == child)
                return it.key();
        }
        return -1;
    }
    QAccessibleInterface *focusChild() const override
    {
        const auto *strip = static_cast<const TimelineStrip *>(object());
        return strip ? child(strip->focusedBar()) : nullptr;
    }
    QAccessibleInterface *childAt(int x, int y) const override
    {
        auto *strip = static_cast<TimelineStrip *>(object());
        if (!strip)
            return nullptr;
        return child(strip->barIndexAt(strip->mapFromGlobal(QPoint(x, y))));
    }

private:
    mutable QVector<QObject *> m_proxies;
    mutable QHash<int, TimelineBarAccessible *> m_bars;
};

QAccessibleInterface *timelineAccessibleFactory(const QString &, QObject *object)
{
    if (auto *strip = qobject_cast<TimelineStrip *>(object))
        return new TimelineStripAccessible(strip);
    return nullptr;
}

struct TimelineAccessibleRegistrar {
    TimelineAccessibleRegistrar() { QAccessible::installFactory(timelineAccessibleFactory); }
};

namespace {
TimelineAccessibleRegistrar g_timelineAccessibleRegistrar;
} // namespace

TimelineStrip::TimelineStrip(IClipboardStorage *storage, QWidget *parent)
    : QWidget(parent), m_storage(storage)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus); // U10: Tab reaches the strip, arrows move
    setAccessibleName(tr("Clipboard timeline"));
    setAccessibleDescription(tr("Entries per day for the last 14 days"));
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

void TimelineStrip::activateBar(int index)
{
    // Clicking an empty bar, outside the strip, or the bar that is already
    // active clears the day filter.
    if (index < 0 || index >= m_bins.size() || m_bins[index].count == 0 || index == m_selected) {
        if (m_selected != -1) {
            m_selected = -1;
            update();
        }
        emit daySelected(0, 0);
        return;
    }
    m_selected = index;
    update();
    const qint64 from = m_bins[index].dayStartMs;
    emit daySelected(from, from + kDayMs - 1);
}

void TimelineStrip::setFocusedBar(int index)
{
    if (index < -1 || index >= m_bins.size() || index == m_focusedBar)
        return;
    m_focusedBar = index;
    update();
    if (QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(this)) {
        if (QAccessibleInterface *bar = iface->child(index))
            QAccessible::updateAccessibility(new QAccessibleEvent(bar, QAccessible::Focus));
    }
}

void TimelineStrip::focusInEvent(QFocusEvent *event)
{
    // Arriving by Tab lands the cursor on the active filter, else on today.
    if (m_focusedBar < 0 || m_focusedBar >= m_bins.size())
        setFocusedBar(m_selected >= 0 && m_selected < m_bins.size() ? m_selected
                                                                    : m_bins.size() - 1);
    else
        update(); // repaint the focus ring
    QWidget::focusInEvent(event);
}

void TimelineStrip::keyPressEvent(QKeyEvent *event)
{
    const int n = m_bins.size();
    if (n == 0) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int last = n - 1;
    if (m_focusedBar < 0 || m_focusedBar >= n)
        m_focusedBar = last; // start at today, like the mouse-facing default
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
        setFocusedBar(last);
        event->accept();
        return;
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Space:
        activateBar(m_focusedBar);
        event->accept();
        return;
    case Qt::Key_Escape:
        activateBar(-1);
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

QRect TimelineStrip::barRect(int index) const
{
    const int n = m_bins.size();
    if (index < 0 || index >= n)
        return {};
    const QFontMetrics captionMetrics(captionFont(font()));
    const DesignTokens::TimelineGeometry geometry =
        DesignTokens::timelineGeometry(size(), n, captionMetrics.height());
    int maxCount = 1;
    for (const auto &bin : m_bins)
        maxCount = qMax(maxCount, bin.count);
    const int count = m_bins.at(index).count;
    const int height = count == 0 ? 2 : qMax(4, geometry.barHeight * count / maxCount);
    return {geometry.left + index * geometry.stride, geometry.top + geometry.barHeight - height,
            geometry.barWidth, height};
}

QString TimelineStrip::barAccessibleName(int index) const
{
    if (index < 0 || index >= m_bins.size())
        return {};
    const QDate day = QDateTime::fromMSecsSinceEpoch(m_bins[index].dayStartMs).date();
    const QDate today = QDate::currentDate();
    QString dayLabel;
    if (day == today)
        dayLabel = tr("Today");
    else if (day == today.addDays(-1))
        dayLabel = tr("Yesterday");
    else
        dayLabel = QLocale::system().dayName(day.dayOfWeek());
    return tr("%n entrie(s), %1", nullptr, m_bins[index].count).arg(dayLabel);
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

        const QRect bar = barRect(i);
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
        if (hasFocus() && i == m_focusedBar) {
            // Keyboard cursor: theme Highlight ring, same language as inputs.
            p.setPen(QPen(DesignTokens::focusRingColor(palette()),
                          DesignTokens::FocusRingWidth));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(bar.adjusted(-2, -2, 2, 2), DesignTokens::RadiusL,
                              DesignTokens::RadiusL);
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
    activateBar(barIndexAt(event->pos()));
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
