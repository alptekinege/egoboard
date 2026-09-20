#pragma once

#include <QFontMetrics>
#include <QStyledItemDelegate>

class BookmarkManager;
class SettingsManager;

// Paints one history row: type icon, preview line, meta line (relative time,
// source app, size), pin marker, sensitive-data shield and group color dots.
class EntryDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    // A group the entry belongs to: the color dot in the list, the name in the
    // badge tooltip.
    struct GroupBadge {
        QString name;
        QColor color;
    };

    explicit EntryDelegate(BookmarkManager *bookmarks, SettingsManager *settings,
                           QObject *parent = nullptr);

    // Vertical breathing room per row in pixels (list density).
    void setRowPadding(int padding) { m_rowPadding = padding; }

    // Words/phrases from the active search, highlighted in the preview line.
    void setSearchTerms(const QStringList &terms) { m_searchTerms = terms; }
    // Row extras (R2, all default off): 1-based entry index prefix, use-count
    // badge in the meta line, sticky day-header rows handled by the view.
    void setShowEntryIndex(bool show) { m_showEntryIndex = show; }
    void setShowUseCountBadge(bool show) { m_showUseCountBadge = show; }
    bool showEntryIndex() const { return m_showEntryIndex; }
    bool showUseCountBadge() const { return m_showUseCountBadge; }
    // Compact one-line header text for group-by-day mode ("Monday, Sep 20").
    static QString dayHeaderText(qint64 timestampMs);

    // Row height for a given density; shared with the settings preview and the
    // metrics test so the sample cannot drift from the list.
    static QSize rowSizeHint(const QFontMetrics &metrics, int rowPadding, int width);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    // Tooltips for the painted badges (pin, sensitive shield, group dots).
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override;

public slots:
    void clearGroupCache();

private:
    // Timestamp style / clock format, cached from SettingsManager.
    void refreshTimeFormats();

    QVector<GroupBadge> groupBadges(qint64 entryId) const;
    // Draws one text line, marking every search-term match with the palette's
    // highlight color. Falls back to a plain draw when nothing matches.
    void drawHighlightedText(QPainter *painter, const QRect &rect, const QString &text,
                             const QPalette &palette, const QColor &textColor,
                             bool selected) const;

    BookmarkManager *m_bookmarks = nullptr;
    SettingsManager *m_settings = nullptr;
    mutable QHash<qint64, QVector<GroupBadge>> m_groupCache; // per visible entry
    QStringList m_searchTerms;
    bool m_showEntryIndex = false;
    bool m_showUseCountBadge = false;
    int m_rowPadding = 8;
    bool m_absoluteTimestamps = false;
    bool m_ampmClock = false;
};
