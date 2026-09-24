#include "GroupTreeModel.h"

#include <QDataStream>
#include <QIcon>
#include <QIODevice>
#include <QMimeData>

#include <functional>
#include <memory>

namespace {
constexpr auto kEntryMime = "application/x-egoboard-entry-ids";
constexpr auto kGroupMime = "application/x-egoboard-group-ids";
} // namespace

struct GroupTreeModel::Node {
    BookmarkGroup group;
    Node *parent = nullptr; // m_root for top-level groups
    std::vector<std::unique_ptr<Node>> children; // stable pointers across growth
};

GroupTreeModel::GroupTreeModel(BookmarkManager *bookmarks, QObject *parent)
    : QAbstractItemModel(parent)
    , m_bookmarks(bookmarks)
{
    rebuild();
    connect(m_bookmarks, &BookmarkManager::groupsChanged, this, [this] { rebuild(); });
    connect(m_bookmarks, &BookmarkManager::membershipChanged, this, [this](qint64) {
        // Counts can change at any depth. Notify each node so nested rows
        // repaint without resetting the tree or collapsing expanded groups.
        const QVector<int> roles{EntryCountRole, Qt::DisplayRole, Qt::ToolTipRole};
        std::function<void(const QModelIndex &)> notify = [this, &roles, &notify](const QModelIndex &parent) {
            for (int row = 0; row < rowCount(parent); ++row) {
                const QModelIndex child = index(row, 0, parent);
                emit dataChanged(child, child, roles);
                notify(child);
            }
        };
        notify({});
    });
}

GroupTreeModel::~GroupTreeModel() = default;

void GroupTreeModel::rebuild()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    const QVector<BookmarkGroup> groups = m_bookmarks->groups();
    QHash<qint64, Node *> byId;

    for (const BookmarkGroup &group : groups) {
        if (group.parentId == 0) {
            auto node = std::make_unique<Node>();
            node->group = group;
            node->parent = m_root.get();
            m_root->children.push_back(std::move(node));
            byId.insert(group.id, m_root->children.back().get());
        }
    }
    // Groups() is name-ordered, not hierarchy-ordered: attach non-top-level
    // groups by iterating until fixed point (handles any ordering; orphans of
    // missing parents are dropped rather than looped forever).
    QList<BookmarkGroup> pending;
    for (const BookmarkGroup &group : groups)
        if (group.parentId != 0)
            pending.append(group);
    bool attached = true;
    while (!pending.isEmpty() && attached) {
        attached = false;
        for (int i = pending.size() - 1; i >= 0; --i) {
            const BookmarkGroup group = pending.at(i);
            auto it = byId.constFind(group.parentId);
            if (it != byId.constEnd()) {
                auto node = std::make_unique<Node>();
                node->group = group;
                node->parent = it.value();
                Node *raw = node.get();
                it.value()->children.push_back(std::move(node));
                byId.insert(group.id, raw);
                pending.removeAt(i);
                attached = true;
            }
        }
    }
    endResetModel();
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
    // The parent pointer makes this walk-free: only the parent's row within its
    // own siblings has to be found (the old version scanned the whole tree).
    const Node *grandParent = parentNode->parent ? parentNode->parent : m_root.get();
    for (size_t row = 0; row < grandParent->children.size(); ++row) {
        if (grandParent->children.at(row).get() == parentNode)
            return createIndex(int(row), 0, const_cast<Node *>(parentNode));
    }
    return {};
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
    switch (role) {
    case Qt::DisplayRole:
        return QStringLiteral("%1 (%2)").arg(group.name).arg(m_bookmarks->entryCount(group.id));
    case Qt::DecorationRole:
        return QIcon::fromTheme(group.icon.isEmpty() ? QStringLiteral("folder") : group.icon);
    case Qt::ToolTipRole: {
        const int count = m_bookmarks->entryCount(group.id);
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
        return m_bookmarks->entryCount(group.id);
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
    QDataStream stream(data->data(QString::fromLatin1(kEntryMime)));
    QList<qint64> ids;
    stream >> ids;
    return stream.status() == QDataStream::Ok ? ids.size() : 0;
}

QMimeData *GroupTreeModel::mimeData(const QModelIndexList &indexes) const
{
    QList<qint64> ids;
    for (const QModelIndex &index : indexes) {
        const Node *node = nodeForIndex(index);
        if (node && !ids.contains(node->group.id))
            ids.append(node->group.id);
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
        QDataStream stream(&encoded, QIODevice::ReadOnly);
        QList<qint64> entryIds;
        stream >> entryIds;
        if (entryIds.isEmpty() || targetGroupId == 0)
            return false; // entries must land on a concrete group
        emit entriesDropped(entryIds, targetGroupId);
        return true;
    }

    if (data->hasFormat(QString::fromLatin1(kGroupMime))) {
        QByteArray encoded = data->data(QString::fromLatin1(kGroupMime));
        QDataStream stream(&encoded, QIODevice::ReadOnly);
        QList<qint64> groupIds;
        stream >> groupIds;
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
