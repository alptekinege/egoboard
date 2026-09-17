#include "ClipboardListModel.h"

#include <QDataStream>
#include <QDateTime>
#include <QIODevice>
#include <QMimeData>
#include <QSet>

#include <algorithm>
#include <functional>

namespace {
constexpr auto kEntryMime = "application/x-egoboard-entry-ids";
}

ClipboardListModel::ClipboardListModel(IClipboardStorage *storage, QObject *parent)
    : QAbstractListModel(parent)
    , m_storage(storage)
{
    connectStorage();
}

void ClipboardListModel::connectStorage()
{
    if (!m_storage)
        return;
    connect(m_storage, &IClipboardStorage::entryAdded, this, &ClipboardListModel::onEntryAdded);
    connect(m_storage, &IClipboardStorage::entryTouched, this, &ClipboardListModel::onEntryTouched);
    connect(m_storage, &IClipboardStorage::entriesRemoved, this, &ClipboardListModel::onEntriesRemoved);
    // A reset (import, clear) invalidates the whole window; reload from page 1.
    connect(m_storage, &IClipboardStorage::storageReset, this, &ClipboardListModel::refresh);
    connect(m_storage, &IClipboardStorage::pinnedChanged, this, &ClipboardListModel::onPinnedChanged);
}

void ClipboardListModel::onEntryAdded(qint64 id)
{
    if (!m_storage)
        return;
    // A new capture can only be spliced in at the top of an unfiltered Newest
    // view; anything else may exclude or re-order it, so reload instead.
    if (!m_filter.isTrivial()) {
        refresh();
        return;
    }
    ClipboardRecord summary;
    if (!m_storage->fetchSummary(id, &summary))
        return;
    beginInsertRows({}, 0, 0);
    m_rows.prepend(summary);
    endInsertRows();
}

void ClipboardListModel::onEntryTouched(qint64 id)
{
    if (!m_storage)
        return;
    const int row = rowForId(id);
    if (row < 0) {
        // The entry is not in the loaded window; a filtered view may now
        // include it, so reload there and leave the plain view alone.
        if (!m_filter.isTrivial())
            refresh();
        return;
    }
    if (m_filter.sortMode != FilterSpec::SortMode::Newest) {
        refresh(); // the touch changes the sort key, not just the row
        return;
    }
    ClipboardRecord summary;
    if (!m_storage->fetchSummary(id, &summary))
        return;
    replaceRow(row, summary);
    if (row > 0) {
        // A touch bumps the timestamp: it belongs at the top again.
        beginMoveRows({}, row, row, {}, 0);
        m_rows.move(row, 0);
        endMoveRows();
    }
}

void ClipboardListModel::onEntriesRemoved(const QList<qint64> &ids)
{
    if (ids.isEmpty())
        return;
    QSet<qint64> unique(ids.cbegin(), ids.cend());
    QList<int> rows;
    rows.reserve(unique.size());
    for (const qint64 id : unique) {
        const int row = rowForId(id);
        if (row >= 0)
            rows.append(row);
    }
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end(), std::greater<int>()); // descending
    // Remove contiguous runs bottom-up so the earlier indices stay valid.
    int i = 0;
    while (i < rows.size()) {
        const int last = rows.at(i);
        int first = last;
        while (i + 1 < rows.size() && rows.at(i + 1) == first - 1) {
            ++i;
            first = rows.at(i);
        }
        beginRemoveRows({}, first, last);
        m_rows.remove(first, last - first + 1);
        endRemoveRows();
        ++i;
    }
}

void ClipboardListModel::onPinnedChanged(qint64 id, bool pinned)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    m_rows[row].pinned = pinned;
    emit dataChanged(index(row), index(row), {PinnedRole, Qt::DisplayRole});
}

void ClipboardListModel::replaceRow(int row, const ClipboardRecord &record)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_rows[row] = record;
    emit dataChanged(index(row), index(row));
}

int ClipboardListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ClipboardListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const ClipboardRecord &record = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return record.id;
    case TimestampRole:
        return record.timestamp;
    case TypeRole:
        return static_cast<int>(record.type);
    case PreviewRole:
    case Qt::DisplayRole:
        return record.preview;
    case SourceAppRole:
        return record.sourceApp;
    case SourceWindowRole:
        return record.sourceWindow;
    case PinnedRole:
        return record.pinned;
    case SensitiveRole:
        return record.sensitive;
    case SizeRole:
        return record.sizeBytes;
    case UseCountRole:
        return record.useCount;
    case HashRole:
        return record.hash;
    case Qt::AccessibleTextRole: {
        // The delegate conveys type/pin/sensitive state by color and badges;
        // screen readers need the same information as text.
        QString text = record.preview.isEmpty() ? tr("Empty entry") : record.preview;
        QStringList bits;
        switch (record.type) {
        case ContentType::Text: bits << tr("Text"); break;
        case ContentType::RichText: bits << tr("Rich text"); break;
        case ContentType::Image: bits << tr("Image"); break;
        case ContentType::Files: bits << tr("Files"); break;
        }
        if (record.pinned) bits << tr("Pinned");
        if (record.sensitive) bits << tr("Sensitive");
        if (!record.sourceApp.isEmpty()) bits << record.sourceApp;
        return text + QStringLiteral(" (%1)").arg(bits.join(QStringLiteral(", ")));
    }
    case Qt::AccessibleDescriptionRole: {
        const QString when = QDateTime::fromMSecsSinceEpoch(record.timestamp)
                                 .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        return record.pinned ? tr("Pinned entry from %1").arg(when)
                             : tr("Copied %1").arg(when);
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> ClipboardListModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();
    roles.insert(IdRole, "id");
    roles.insert(TimestampRole, "timestamp");
    roles.insert(TypeRole, "type");
    roles.insert(PreviewRole, "preview");
    roles.insert(SourceAppRole, "sourceApp");
    roles.insert(SourceWindowRole, "sourceWindow");
    roles.insert(PinnedRole, "pinned");
    roles.insert(SensitiveRole, "sensitive");
    roles.insert(SizeRole, "size");
    roles.insert(UseCountRole, "useCount");
    roles.insert(HashRole, "hash");
    return roles;
}

bool ClipboardListModel::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && m_canFetchMore;
}

void ClipboardListModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid() || !m_canFetchMore || !m_storage)
        return;

    PageCursor cursor;
    if (!m_rows.isEmpty()) {
        cursor.valid = true;
        cursor.timestampMs = m_rows.last().timestamp;
        cursor.id = m_rows.last().id;
        cursor.useCount = m_rows.last().useCount; // MostUsed sort mode
    }
    bool hasMore = false;
    const auto page = m_storage->fetchPage(m_filter, cursor, kPageSize, &hasMore);
    if (page.isEmpty()) {
        m_canFetchMore = false;
        emit initialPageLoaded(m_rows.isEmpty());
        return;
    }

    beginInsertRows({}, m_rows.size(), m_rows.size() + page.size() - 1);
    m_rows.append(page);
    endInsertRows();
    m_canFetchMore = hasMore;
    emit initialPageLoaded(m_rows.isEmpty());
}

Qt::ItemFlags ClipboardListModel::flags(const QModelIndex &index) const
{
    auto flags = QAbstractListModel::flags(index);
    if (index.isValid())
        flags |= Qt::ItemIsDragEnabled;
    return flags;
}

QStringList ClipboardListModel::mimeTypes() const
{
    return {QString::fromLatin1(kEntryMime), QStringLiteral("text/plain")};
}

QMimeData *ClipboardListModel::mimeData(const QModelIndexList &indexes) const
{
    QList<qint64> ids;
    QString previews;
    for (const QModelIndex &index : indexes) {
        if (!index.isValid())
            continue;
        const qint64 id = m_rows.at(index.row()).id;
        if (!ids.contains(id))
            ids.append(id);
        previews += m_rows.at(index.row()).preview + QLatin1Char('\n');
    }
    if (ids.isEmpty())
        return nullptr;
    auto *mime = new QMimeData;
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << ids;
    mime->setData(QString::fromLatin1(kEntryMime), encoded);
    mime->setText(previews.trimmed());
    return mime;
}

void ClipboardListModel::setFilter(const FilterSpec &filter)
{
    beginResetModel();
    m_filter = filter;
    m_rows.clear();
    m_canFetchMore = true;
    endResetModel();
    fetchMore({});
}

void ClipboardListModel::refresh()
{
    setFilter(m_filter);
}

qint64 ClipboardListModel::idAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).id : 0;
}

int ClipboardListModel::rowForId(qint64 id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id)
            return i;
    }
    return -1;
}

ClipboardRecord ClipboardListModel::recordAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row) : ClipboardRecord{};
}

std::optional<ClipboardRecord> ClipboardListModel::recordForId(qint64 id) const
{
    const int row = rowForId(id);
    if (row < 0)
        return std::nullopt;
    return m_rows.at(row);
}
