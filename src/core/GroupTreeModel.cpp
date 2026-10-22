#include "GroupTreeModel.h"

#include "IClipboardStorage.h"

#include <QDataStream>
#include <QIcon>
#include <QIODevice>
#include <QMimeData>
#include <QSet>

#include <functional>
#include <memory>

namespace {
constexpr auto kEntryMime = "application/x-egoboard-entry-ids";
constexpr auto kGroupMime = "application/x-egoboard-group-ids";
// Drag payloads come from any app: bound the allocation before QDataStream
// can act on its length prefix (crafted FFFFFFFF -> multi-GB QList).
constexpr int kMaxDragBytes = 64 * 1024;
constexpr int kMaxDragIds = 1024;

bool decodeIdList(const QByteArray &encoded, QList<qint64> *out)
{
    if (!out || encoded.isEmpty() || encoded.size() > kMaxDragBytes)
        return false;
    QDataStream stream(encoded);
    stream.setVersion(QDataStream::Qt_6_0);
    QList<qint64> ids;
    stream >> ids;
    if (stream.status() != QDataStream::Ok || ids.size() > kMaxDragIds)
        return false;
    for (const qint64 id : ids) {
        if (id <= 0)
            return false;
    }
    *out = ids;
    return true;
}
} // namespace

struct GroupTreeModel::Node {
    BookmarkGroup group;
    Node *parent = nullptr; // m_root for top-level groups
    int row = 0; // sibling position; the tree only changes during rebuild()
    std::vector<std::unique_ptr<Node>> children; // stable pointers across growth
};

GroupTreeModel::GroupTreeModel(BookmarkManager *bookmarks, QObject *parent,
                               IClipboardStorage *storage)
    : QAbstractItemModel(parent)
    , m_bookmarks(bookmarks)
    , m_storage(storage)
{
    rebuild();
    connect(m_bookmarks, &BookmarkManager::groupsChanged, this, [this] { rebuild(); });
    connect(m_bookmarks, &BookmarkManager::membershipCountChanged, this,
            [this](qint64 groupId, int delta) {
                const int count = qMax(0, m_entryCounts.value(groupId) + delta);
                m_entryCounts.insert(groupId, count);
                const QModelIndex changed = indexForGroup(groupId);
                if (changed.isValid())
                    emit dataChanged(changed, changed,
                                     {EntryCountRole, Qt::DisplayRole, Qt::ToolTipRole});
    });
    if (m_storage) {
        connect(m_storage, &IClipboardStorage::entriesRemoved, this,
                [this](const QList<qint64> &) { refreshEntryCounts(); });
        connect(m_storage, &IClipboardStorage::storageReset, this,
                &GroupTreeModel::rebuild);
    }
}

GroupTreeModel::~GroupTreeModel() = default;

void GroupTreeModel::rebuild()
{
    beginResetModel();
    m_entryCounts = m_bookmarks->entryCounts();
    m_root = std::make_unique<Node>();
    const QVector<BookmarkGroup> groups = m_bookmarks->groups();
    // Groups() is name-ordered, not hierarchy-ordered. Build parent-to-child
    // adjacency first, then walk out from roots so even reverse-sorted deep
    // trees are assembled in linear time. Unreachable or cyclic groups stay
    // detached, matching the previous fixed-point behavior.
    std::vector<std::unique_ptr<Node>> owned;
    owned.reserve(size_t(groups.size()));
    std::vector<Node *> byIndex;
    byIndex.reserve(size_t(groups.size()));
    QHash<qint64, QVector<int>> childrenByParent;
    childrenByParent.reserve(groups.size());
    for (int i = 0; i < groups.size(); ++i) {
        const BookmarkGroup &group = groups.at(i);
        auto node = std::make_unique<Node>();
        node->group = group;
        byIndex.push_back(node.get());
        owned.push_back(std::move(node));
        if (group.parentId != 0)
            childrenByParent[group.parentId].append(i);
    }

    QList<int> pending;
    pending.reserve(groups.size());
    for (int i = 0; i < groups.size(); ++i) {
        if (groups.at(i).parentId != 0)
            continue;
        Node *node = byIndex[size_t(i)];
        node->parent = m_root.get();
        node->row = int(m_root->children.size());
        m_root->children.push_back(std::move(owned[size_t(i)]));
        pending.append(i);
    }
    for (qsizetype next = 0; next < pending.size(); ++next) {
        Node *parent = byIndex[size_t(pending.at(next))];
        const auto children = childrenByParent.constFind(parent->group.id);
        if (children == childrenByParent.cend())
            continue;
        for (const int childIndex : children.value()) {
            auto &child = owned[size_t(childIndex)];
            if (!child)
                continue;
            child->parent = parent;
            child->row = int(parent->children.size());
            parent->children.push_back(std::move(child));
            pending.append(childIndex);
        }
    }
    endResetModel();
}

void GroupTreeModel::refreshEntryCounts()
{
    // Entry removal cascades membership rows in SQLite. One grouped query
    // refreshes those counts; routine membership edits use the delta signal.
    m_entryCounts = m_bookmarks->entryCounts();
    const QVector<int> roles{EntryCountRole, Qt::DisplayRole, Qt::ToolTipRole};
    std::function<void(const QModelIndex &)> notify = [this, &roles, &notify](const QModelIndex &parent) {
        for (int row = 0; row < rowCount(parent); ++row) {
            const QModelIndex child = index(row, 0, parent);
            emit dataChanged(child, child, roles);
            notify(child);
        }
    };
    notify({});
}

QModelIndex GroupTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};
    const Node *parentNode = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    if (!parentNode || row >= int(parentNode->children.size()))
        return {};
    return createIndex(row, column, parentNode->children.at(size_t(row)).get());
}

QModelIndex GroupTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    const Node *childNode = static_cast<const Node *>(child.internalPointer());
    if (!childNode)
        return {};
    const Node *parentNode = childNode->parent;
    if (!parentNode || parentNode == m_root.get())
        return {}; // top level
    return createIndex(parentNode->row, 0, const_cast<Node *>(parentNode));
}

int GroupTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    const Node *node = parent.isValid() ? nodeForIndex(parent) : m_root.get();
    return node ? int(node->children.size()) : 0;
}

int GroupTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant GroupTreeModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeForIndex(index);
    if (!node)
        return {};
    const BookmarkGroup &group = node->group;
    const int count = m_entryCounts.value(group.id);
    switch (role) {
    case Qt::DisplayRole:
        return QStringLiteral("%1 (%2)").arg(group.name).arg(count);
    case Qt::DecorationRole:
        return QIcon::fromTheme(group.icon.isEmpty() ? QStringLiteral("folder") : group.icon);
    case Qt::ToolTipRole: {
        return tr("%1 — %2 %3")
            .arg(group.name)
            .arg(count)
            .arg(count == 1 ? tr("entry") : tr("entries"));
    }
    case GroupIdRole:
        return group.id;
    case ColorRole:
        return group.color;
    case IconRole:
        return group.icon;
    case EntryCountRole:
        return count;
    default:
        return {};
    }
}

QHash<int, QByteArray> GroupTreeModel::roleNames() const
{
    auto roles = QAbstractItemModel::roleNames();
    roles.insert(GroupIdRole, "groupId");
    roles.insert(ColorRole, "color");
    roles.insert(IconRole, "icon");
    roles.insert(EntryCountRole, "entryCount");
    return roles;
}

Qt::ItemFlags GroupTreeModel::flags(const QModelIndex &index) const
{
    auto flags = QAbstractItemModel::flags(index);
    if (index.isValid())
        flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    else
        flags |= Qt::ItemIsDropEnabled; // top level
    return flags;
}

QStringList GroupTreeModel::mimeTypes() const
{
    return {QString::fromLatin1(kGroupMime), QString::fromLatin1(kEntryMime)};
}

int GroupTreeModel::entryCount(const QMimeData *data)
{
    if (!data || !data->hasFormat(QString::fromLatin1(kEntryMime)))
        return 0;
    QList<qint64> ids;
    if (!decodeIdList(data->data(QString::fromLatin1(kEntryMime)), &ids))
        return 0;
    return ids.size();
}

QMimeData *GroupTreeModel::mimeData(const QModelIndexList &indexes) const
{
    QSet<qint64> seen;
    seen.reserve(indexes.size());
    QList<qint64> ids;
    ids.reserve(indexes.size());
    for (const QModelIndex &index : indexes) {
        const Node *node = nodeForIndex(index);
        if (node && !seen.contains(node->group.id)) {
            seen.insert(node->group.id);
            ids.append(node->group.id);
        }
    }
    if (ids.isEmpty())
        return nullptr;
    auto mime = std::make_unique<QMimeData>();
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << ids;
    mime->setData(QString::fromLatin1(kGroupMime), encoded);
    return mime.release();
}

bool GroupTreeModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int,
                                     const QModelIndex &) const
{
    return data->hasFormat(QString::fromLatin1(kGroupMime))
        || data->hasFormat(QString::fromLatin1(kEntryMime));
}

bool GroupTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int, int,
                                 const QModelIndex &parent)
{
    if (action == Qt::IgnoreAction)
        return false;

    qint64 targetGroupId = 0;
    const Node *parentNode = parent.isValid() ? nodeForIndex(parent) : nullptr;
    if (parentNode)
        targetGroupId = parentNode->group.id;

    if (data->hasFormat(QString::fromLatin1(kEntryMime))) {
        QByteArray encoded = data->data(QString::fromLatin1(kEntryMime));
        QList<qint64> entryIds;
        if (!decodeIdList(encoded, &entryIds))
            return false;
        if (entryIds.isEmpty() || targetGroupId == 0)
            return false; // entries must land on a concrete group
        emit entriesDropped(entryIds, targetGroupId);
        return true;
    }

    if (data->hasFormat(QString::fromLatin1(kGroupMime))) {
        QByteArray encoded = data->data(QString::fromLatin1(kGroupMime));
        QList<qint64> groupIds;
        if (!decodeIdList(encoded, &groupIds))
            return false;
        bool moved = false;
        for (const qint64 id : groupIds)
            moved = m_bookmarks->moveGroup(id, targetGroupId) || moved;
        return moved;
    }
    return false;
}

const GroupTreeModel::Node *GroupTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_root.get();
    return static_cast<const Node *>(index.internalPointer());
}

std::optional<BookmarkGroup> GroupTreeModel::groupForIndex(const QModelIndex &index) const
{
    const Node *node = nodeForIndex(index);
    if (!node || node == m_root.get())
        return std::nullopt;
    return node->group;
}

QModelIndex GroupTreeModel::indexForGroup(qint64 groupId) const
{
    std::function<QModelIndex(const Node *)> walk = [&](const Node *node) -> QModelIndex {
        for (size_t i = 0; i < node->children.size(); ++i) {
            const Node *child = node->children.at(i).get();
            if (child->group.id == groupId)
                return createIndex(int(i), 0, const_cast<Node *>(child));
            const QModelIndex deeper = walk(child);
            if (deeper.isValid())
                return deeper;
        }
        return {};
    };
    return walk(m_root.get());
}
