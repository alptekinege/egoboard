#include "ClipboardListModel.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

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
    connect(m_storage, &IClipboardStorage::entryAdded, this, &ClipboardListModel::refresh);
    connect(m_storage, &IClipboardStorage::entryTouched, this, &ClipboardListModel::refresh);
    connect(m_storage, &IClipboardStorage::entriesRemoved, this, &ClipboardListModel::refresh);
    connect(m_storage, &IClipboardStorage::storageReset, this, &ClipboardListModel::refresh);
    // Pinned toggles only change decoration, not order; repaint instead of reload.
    connect(m_storage, &IClipboardStorage::pinnedChanged, this,
            [this](qint64 id, bool) {
                const int row = rowForId(id);
                if (row >= 0)
                    emit dataChanged(index(row), index(row), {PinnedRole, Qt::DisplayRole});
            });
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
