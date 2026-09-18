#pragma once

#include <QStyledItemDelegate>

class BookmarkManager;
class SettingsManager;

// Paints one history row: type icon, preview line, meta line (relative time,
// source app, size), pin marker, sensitive-data shield and group color dots.
class EntryDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit EntryDelegate(BookmarkManager *bookmarks, SettingsManager *settings,
                           QObject *parent = nullptr);

    // Vertical breathing room per row in pixels (list density).
    void setRowPadding(int padding) { m_rowPadding = padding; }

    // Words/phrases from the active search, highlighted in the preview line.
    void setSearchTerms(const QStringList &terms) { m_searchTerms = terms; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

public slots:
    void clearGroupCache();

private:
    // Timestamp style / clock format, cached from SettingsManager.
    void refreshTimeFormats();

    QVector<QColor> groupColors(qint64 entryId) const;
    // Draws one text line, marking every search-term match with the palette's
    // highlight color. Falls back to a plain draw when nothing matches.
    void drawHighlightedText(QPainter *painter, const QRect &rect, const QString &text,
                             const QPalette &palette, bool selected) const;

    BookmarkManager *m_bookmarks = nullptr;
    SettingsManager *m_settings = nullptr;
    mutable QHash<qint64, QVector<QColor>> m_groupColorCache; // per visible entry
    QStringList m_searchTerms;
    int m_rowPadding = 8;
    bool m_absoluteTimestamps = false;
    bool m_ampmClock = false;
};
