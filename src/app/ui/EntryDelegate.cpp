#include "EntryDelegate.h"

#include "BookmarkManager.h"
#include "ClipboardListModel.h"
#include "DesignTokens.h"
#include "UiHelpers.h"
#include "../SettingsManager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

#include <algorithm>

namespace {

QString typeIconName(int typeRole)
{
    switch (static_cast<ContentType>(typeRole)) {
    case ContentType::Image:
        return QStringLiteral("image-x-generic");
    case ContentType::RichText:
        return QStringLiteral("text-html");
    case ContentType::Files:
        return QStringLiteral("folder");
    case ContentType::Text:
    default:
        return QStringLiteral("text-x-generic");
    }
}

QString relativeTime(qint64 timestampMs, bool absolute, bool ampm)
{
    const QDateTime timestamp = QDateTime::fromMSecsSinceEpoch(timestampMs);
    const QDateTime now = QDateTime::currentDateTime();
    const QString timeFormat = ampm ? QStringLiteral("h:mm AP") : QStringLiteral("HH:mm");
    if (absolute)
        return timestamp.toString(QStringLiteral("yyyy-MM-dd ") + timeFormat);
    const qint64 seconds = timestamp.secsTo(now);
    if (seconds < 0)
        return EntryDelegate::tr("in the future");
    if (seconds < 50)
        return EntryDelegate::tr("just now");
    if (seconds < 90 * 60)
        return EntryDelegate::tr("%1 min ago").arg(qRound(seconds / 60.0));
    if (seconds < 24 * 3600)
        return EntryDelegate::tr("%1 h ago").arg(qRound(seconds / 3600.0));
    if (seconds < 7 * 24 * 3600)
        return EntryDelegate::tr("%1 d ago").arg(qRound(seconds / 86400.0));
    return timestamp.toString(QStringLiteral("yyyy-MM-dd ") + timeFormat);
}

// One painted marker on the right-hand side of a row. The rect is shared by
// paint() and helpEvent(), so a tooltip always lands on the badge it explains.
struct Badge {
    QRect rect;
    QString tooltip;
    QIcon icon; // pin / sensitive shield; null for a group color dot
    QColor dot; // group color; invalid for icons
};

QVector<Badge> badgesFor(const QRect &row, const DesignTokens::RowMetrics &metrics, bool pinned,
                         bool sensitive, const QVector<EntryDelegate::GroupBadge> &groups)
{
    QVector<Badge> badges;
    int rightEdge = row.left() + row.width() - metrics.margin;
    const int badgeTop = row.top() + (row.height() - metrics.badgeSize) / 2;
    if (pinned) {
        rightEdge -= metrics.badgeSize;
        badges.append({QRect(rightEdge, badgeTop, metrics.badgeSize, metrics.badgeSize),
                       EntryDelegate::tr("Pinned"),
                       QIcon::fromTheme(QStringLiteral("bookmarks")), {}});
    }
    if (sensitive) {
        rightEdge -= metrics.badgeSize;
        badges.append({QRect(rightEdge, badgeTop, metrics.badgeSize, metrics.badgeSize),
                       EntryDelegate::tr("Flagged as sensitive"),
                       QIcon::fromTheme(QStringLiteral("security-low")), {}});
    }
    for (int i = 0; i < qMin(groups.size(), 4); ++i) {
        // Dots are spaced, never overlapped (DesignTokens::dotAdvance).
        rightEdge -= metrics.dotAdvance();
        const int dotTop = row.top() + (row.height() - metrics.dotDiameter) / 2;
        badges.append({QRect(rightEdge, dotTop, metrics.dotDiameter, metrics.dotDiameter),
                       groups.at(i).name.isEmpty()
                           ? QString()
                           : EntryDelegate::tr("Group: %1").arg(groups.at(i).name),
                       {},
                       groups.at(i).color});
    }
    return badges;
}

// How far the text may run before it would touch the badge strip.
int badgesLeftEdge(const QRect &row, const QVector<Badge> &badges,
                   const DesignTokens::RowMetrics &metrics)
{
    int edge = row.right() - metrics.margin;
    for (const Badge &badge : badges)
        edge = qMin(edge, badge.rect.left() - metrics.margin);
    return edge;
}

} // namespace

EntryDelegate::EntryDelegate(BookmarkManager *bookmarks, SettingsManager *settings,
                             QObject *parent)
    : QStyledItemDelegate(parent)
    , m_bookmarks(bookmarks)
    , m_settings(settings)
{
    refreshTimeFormats();
    if (settings)
        connect(settings, &SettingsManager::changed, this,
                &EntryDelegate::refreshTimeFormats);
}

void EntryDelegate::refreshTimeFormats()
{
    // Cached once per settings change instead of per painted row.
    m_absoluteTimestamps = m_settings && m_settings->timestampStyle() == QLatin1String("absolute");
    m_ampmClock = m_settings && !m_settings->clock24h();
    m_showEntryIndex = m_settings && m_settings->showEntryIndex();
    m_showUseCountBadge = m_settings && m_settings->showUseCountBadge();
    m_groupCache.clear();
}

QString EntryDelegate::dayHeaderText(qint64 timestampMs)
{
    const QDate date = QDateTime::fromMSecsSinceEpoch(timestampMs).date();
    const QDate today = QDate::currentDate();
    if (date == today)
        return tr("Today");
    if (date == today.addDays(-1))
        return tr("Yesterday");
    return QLocale::system().toString(date, QLocale::LongFormat);
}

void EntryDelegate::clearGroupCache()
{
    m_groupCache.clear();
}

QVector<EntryDelegate::GroupBadge> EntryDelegate::groupBadges(qint64 entryId) const
{
    const auto cached = m_groupCache.constFind(entryId);
    if (cached != m_groupCache.constEnd())
        return cached.value();
    QVector<GroupBadge> badges;
    if (m_bookmarks) {
        const QList<qint64> groupIds = m_bookmarks->groupIdsForEntry(entryId);
        for (const qint64 groupId : groupIds) {
            const auto group = m_bookmarks->group(groupId);
            if (group.has_value() && !group->color.isEmpty())
                badges.append({group->name, QColor(group->color)});
        }
    }
    m_groupCache.insert(entryId, badges);
    return badges;
}

void EntryDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    // Let the style draw selection/hover/focus background only.
    opt.text.clear();
    opt.icon = QIcon();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const bool selected = option.state & QStyle::State_Selected;
    const bool enabled = option.state & QStyle::State_Enabled;
    const bool hovered = option.state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // The style paints selection and focus; a palette-derived wash is added on
    // hover so a row answers the pointer before it is clicked.
    if (hovered && !selected && enabled)
        painter->fillRect(option.rect, DesignTokens::hoverBackground(option.palette));

    const DesignTokens::RowMetrics metrics = DesignTokens::rowMetrics(m_rowPadding);
    const int left = option.rect.left();
    const int top = option.rect.top();

    // Type icon.
    const QIcon icon =
        QIcon::fromTheme(typeIconName(index.data(ClipboardListModel::TypeRole).toInt()));
    icon.paint(painter, QRect(left + metrics.margin, top + (option.rect.height() - metrics.iconSize) / 2,
                              metrics.iconSize, metrics.iconSize),
               Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);

    // Right-hand badges.
    const QVector<GroupBadge> groups =
        groupBadges(index.data(ClipboardListModel::IdRole).toLongLong());
    const bool pinned = index.data(ClipboardListModel::PinnedRole).toBool();
    const bool sensitive = index.data(ClipboardListModel::SensitiveRole).toBool();
    const QVector<Badge> badges = badgesFor(option.rect, metrics, pinned, sensitive, groups);
    for (const Badge &badge : badges) {
        if (badge.dot.isValid()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(badge.dot);
            painter->drawEllipse(badge.rect);
        } else {
            badge.icon.paint(painter, badge.rect, Qt::AlignCenter,
                             enabled ? QIcon::Normal : QIcon::Disabled);
        }
    }

    // Text block.
    const int textLeft = metrics.contentLeft(left);
    const int textRight = badgesLeftEdge(option.rect, badges, metrics);
    const int textWidth = qMax(0, textRight - textLeft);
    const QFont originalFont = painter->font();
    QFont previewFont = originalFont;
    previewFont.setWeight(QFont::DemiBold);
    painter->setFont(previewFont);
    QString previewSource = index.data().toString();
    // R2 row extra: 1-based entry index prefix ("12 · text…").
    if (m_showEntryIndex)
        previewSource = tr("%1 · %2").arg(index.row() + 1).arg(previewSource);
    const QString preview =
        painter->fontMetrics().elidedText(previewSource, Qt::ElideRight, textWidth);
    const QRect previewRect(textLeft, top + metrics.padding, textWidth,
                            painter->fontMetrics().height());
    const QColor previewColor =
        DesignTokens::previewTextColor(option.palette, selected, enabled);
    if (m_searchTerms.isEmpty()) {
        painter->setPen(previewColor);
        painter->drawText(previewRect, Qt::AlignVCenter | Qt::AlignLeft, preview);
    } else {
        drawHighlightedText(painter, previewRect, preview, option.palette, previewColor, selected);
    }

    painter->setFont(originalFont);
    painter->setPen(DesignTokens::metaTextColor(option.palette, selected, enabled));
    QStringList metaParts;
    metaParts << relativeTime(index.data(ClipboardListModel::TimestampRole).toLongLong(),
                              m_absoluteTimestamps, m_ampmClock);
    const QString app = index.data(ClipboardListModel::SourceAppRole).toString();
    if (!app.isEmpty())
        metaParts << app;
    const qint64 sizeBytes = index.data(ClipboardListModel::SizeRole).toLongLong();
    const QString sizeText = UiHelpers::humanSize(sizeBytes);
    if (!sizeText.isEmpty())
        metaParts << sizeText;
    // use_count counts re-copies/pastes after the initial capture, so the
    // badge shows the stored value rather than fudging it by one.
    const int useCount = index.data(ClipboardListModel::UseCountRole).toInt();
    if (useCount > 0)
        metaParts << tr("used %1×").arg(useCount);
    // R2 row extra: explicit use-count badge even at zero ("0 pastes").
    if (m_showUseCountBadge && useCount == 0)
        metaParts << tr("0 pastes");
    const QString meta = painter->fontMetrics().elidedText(metaParts.join(QStringLiteral(" · ")),
                                                           Qt::ElideRight, textWidth);
    painter->drawText(QRect(textLeft, top + metrics.padding + painter->fontMetrics().height() + 2,
                            textWidth, painter->fontMetrics().height()),
                      Qt::AlignVCenter | Qt::AlignLeft, meta);

    painter->restore();
}

QSize EntryDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    return rowSizeHint(QFontMetrics(option.font), m_rowPadding, option.rect.width());
}

QSize EntryDelegate::rowSizeHint(const QFontMetrics &metrics, int rowPadding, int width)
{
    return DesignTokens::rowMetrics(rowPadding).sizeHint(metrics, width);
}

bool EntryDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                              const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (!event || !view || !index.isValid() || event->type() != QEvent::ToolTip)
        return QStyledItemDelegate::helpEvent(event, view, option, index);

    const DesignTokens::RowMetrics metrics = DesignTokens::rowMetrics(m_rowPadding);
    const QVector<Badge> badges =
        badgesFor(option.rect, metrics, index.data(ClipboardListModel::PinnedRole).toBool(),
                  index.data(ClipboardListModel::SensitiveRole).toBool(),
                  groupBadges(index.data(ClipboardListModel::IdRole).toLongLong()));
    const QPoint pos = event->pos();
    for (const Badge &badge : badges) {
        if (badge.tooltip.isEmpty() || !badge.rect.contains(pos))
            continue;
        QToolTip::showText(event->globalPos(), badge.tooltip, view, badge.rect);
        return true;
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

void EntryDelegate::drawHighlightedText(QPainter *painter, const QRect &rect, const QString &text,
                                        const QPalette &palette, const QColor &textColor,
                                        bool selected) const
{
    // Collect case-insensitive match ranges for every search term.
    const QString lower = text.toLower();
    QVector<QPair<int, int>> ranges;
    for (const QString &term : m_searchTerms) {
        const QString needle = term.toLower();
        if (needle.isEmpty())
            continue;
        int from = 0;
        while (ranges.size() < 64) {
            const int at = lower.indexOf(needle, from);
            if (at < 0)
                break;
            ranges.append({at, at + needle.size()});
            from = at + needle.size();
        }
    }
    if (ranges.isEmpty()) {
        painter->setPen(textColor);
        painter->drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, text);
        return;
    }
    std::sort(ranges.begin(), ranges.end());
    QVector<QPair<int, int>> merged;
    for (const auto &range : ranges) {
        if (!merged.isEmpty() && range.first <= merged.last().second)
            merged.last().second = qMax(merged.last().second, range.second);
        else
            merged.append(range);
    }

    const QFontMetrics metrics = painter->fontMetrics();
    const QColor fill = DesignTokens::searchHighlightFill(palette, selected);
    const int baseline = rect.top() + (rect.height() + metrics.ascent() - metrics.descent()) / 2;

    int x = rect.left();
    int position = 0;
    const auto drawRun = [&](int from, int to, bool highlight) {
        if (to <= from)
            return;
        const QString piece = text.mid(from, to - from);
        const int width = metrics.horizontalAdvance(piece);
        if (highlight)
            painter->fillRect(QRect(x, baseline - metrics.ascent(), width, metrics.height()), fill);
        painter->setPen(textColor);
        painter->drawText(x, baseline, piece);
        x += width;
    };
    for (const auto &range : merged) {
        drawRun(position, range.first, false);
        drawRun(range.first, range.second, true);
        position = range.second;
    }
    drawRun(position, text.size(), false);
}
