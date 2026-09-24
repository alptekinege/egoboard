#pragma once

#include "BookmarkManager.h"

#include <QAbstractItemModel>

#include <memory>

class QMimeData;

// Tree model over the bookmark group hierarchy. Supports:
//  - dragging entries (application/x-egoboard-entry-ids) onto a group
//  - internal drag-and-drop to re-parent groups (cycle-safe)
class GroupTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles {
        GroupIdRole = Qt::UserRole + 1,
        ColorRole,
        IconRole,
        EntryCountRole,
    };

    explicit GroupTreeModel(BookmarkManager *bookmarks, QObject *parent = nullptr);
    ~GroupTreeModel() override;

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    std::optional<BookmarkGroup> groupForIndex(const QModelIndex &index) const;
    QModelIndex indexForGroup(qint64 groupId) const;
    // How many history entries a drag carries (decoded entry-id list size);
    // 0 for group drags, empty or malformed payloads. Drives the groups
    // tree drop-target count badge (U10).
    static int entryCount(const QMimeData *data);

signals:
    void entriesDropped(const QList<qint64> &entryIds, qint64 groupId);

private:
    struct Node;
    void rebuild();
    const Node *nodeForIndex(const QModelIndex &index) const;

    BookmarkManager *m_bookmarks = nullptr;
    std::unique_ptr<Node> m_root;
};
