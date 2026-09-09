#include "EntryDelegate.h"

#include "BookmarkManager.h"
#include "ClipboardListModel.h"
#include "../SettingsManager.h"

#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QStyle>

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

QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes < 1024)
        return EntryDelegate::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return EntryDelegate::tr("%1 kB").arg(bytes / 1024.0, 0, 'f', 1);
    return EntryDelegate::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
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
    m_groupColorCache.clear();
}

void EntryDelegate::clearGroupCache()
{
    m_groupColorCache.clear();
}

QVector<QColor> EntryDelegate::groupColors(qint64 entryId) const
{
    const auto cached = m_groupColorCache.constFind(entryId);
    if (cached != m_groupColorCache.constEnd())
        return cached.value();
    QVector<QColor> colors;
    if (m_bookmarks) {
        const QList<qint64> groupIds = m_bookmarks->groupIdsForEntry(entryId);
        for (const qint64 groupId : groupIds) {
            const auto group = m_bookmarks->group(groupId);
            if (group.has_value() && !group->color.isEmpty())
                colors.append(QColor(group->color));
        }
    }
    m_groupColorCache.insert(entryId, colors);
    return colors;
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

    const bool isDark = option.palette.color(QPalette::Window).lightness() < 128;
    const QPalette::ColorRole secondaryRole =
        isDark ? QPalette::PlaceholderText : QPalette::WindowText; // subdued tone
    QColor metaColor = option.palette.color(secondaryRole);
    metaColor.setAlphaF(0.75); // Breeze-style muted secondary text
    const QPen metaPen(metaColor);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const int left = option.rect.left();
    const int top = option.rect.top();
    const int height = option.rect.height();
    constexpr int kMargin = 8;
    constexpr int kIconSize = 22;

    // Type icon.
    const QIcon icon = QIcon::fromTheme(
        typeIconName(index.data(ClipboardListModel::TypeRole).toInt()));
    icon.paint(painter, left + kMargin, top + (height - kIconSize) / 2, kIconSize, kIconSize);

    // Right-hand badges.
    int rightEdge = left + option.rect.width() - kMargin;
    const bool pinned = index.data(ClipboardListModel::PinnedRole).toBool();
    if (pinned) {
        const QIcon star = QIcon::fromTheme(QStringLiteral("bookmarks"));
        rightEdge -= 16;
        star.paint(painter, rightEdge, top + (height - 16) / 2, 16, 16);
    }
    const bool sensitive = index.data(ClipboardListModel::SensitiveRole).toBool();
    if (sensitive) {
        const QIcon shield = QIcon::fromTheme(QStringLiteral("security-low"));
        rightEdge -= 16;
        shield.paint(painter, rightEdge, top + (height - 16) / 2, 16, 16);
    }
    const QVector<QColor> colors =
        groupColors(index.data(ClipboardListModel::IdRole).toLongLong());
    for (int i = 0; i < qMin(colors.size(), 4); ++i) {
        rightEdge -= 12;
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.at(i));
        painter->drawEllipse(QPoint(rightEdge + 4, top + height / 2), 4, 4);
    }

    // Text block.
    const int textLeft = left + kMargin + kIconSize + kMargin;
    const int textWidth = rightEdge - textLeft - kMargin;
    const QFont originalFont = painter->font();
    QFont previewFont = originalFont;
    previewFont.setWeight(QFont::DemiBold);
    painter->setFont(previewFont);
    const QString preview =
        painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, textWidth);
    painter->setPen(option.palette.color(
        option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
    painter->drawText(QRect(textLeft, top + kMargin, textWidth, painter->fontMetrics().height()),
                      Qt::AlignVCenter | Qt::AlignLeft, preview);

    painter->setFont(originalFont);
    painter->setPen(metaPen);
    QStringList metaParts;
    metaParts << relativeTime(index.data(ClipboardListModel::TimestampRole).toLongLong(),
                              m_absoluteTimestamps, m_ampmClock);
    const QString app = index.data(ClipboardListModel::SourceAppRole).toString();
    if (!app.isEmpty())
        metaParts << app;
    const qint64 sizeBytes = index.data(ClipboardListModel::SizeRole).toLongLong();
    const QString sizeText = humanSize(sizeBytes);
    if (!sizeText.isEmpty())
        metaParts << sizeText;
    const int useCount = index.data(ClipboardListModel::UseCountRole).toInt();
    if (useCount > 0)
        metaParts << tr("used %1×").arg(useCount + 1);
    const QString meta = painter->fontMetrics().elidedText(metaParts.join(QStringLiteral(" · ")),
                                                           Qt::ElideRight, textWidth);
    painter->drawText(
        QRect(textLeft, top + kMargin + painter->fontMetrics().height() + 2, textWidth,
              painter->fontMetrics().height()),
        Qt::AlignVCenter | Qt::AlignLeft, meta);

    painter->restore();
}

QSize EntryDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    const QFontMetrics metrics(option.font);
    return QSize(option.rect.width(), metrics.height() * 2 + 2 * m_rowPadding + 4);
}
