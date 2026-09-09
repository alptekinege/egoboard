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

    BookmarkManager *m_bookmarks = nullptr;
    SettingsManager *m_settings = nullptr;
    mutable QHash<qint64, QVector<QColor>> m_groupColorCache; // per visible entry
    int m_rowPadding = 8;
    bool m_absoluteTimestamps = false;
    bool m_ampmClock = false;
};
