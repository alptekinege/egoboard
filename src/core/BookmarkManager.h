#pragma once

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QVector>

struct BookmarkGroup {
    qint64 id = 0;
    qint64 parentId = 0;
    QString name;
    QString color; // "#rrggbb" or empty
    QString icon; // icon theme name or empty

    bool isValid() const { return id != 0; }
};

// Manages the (nested) favorite-group tree and entry<->group memberships.
// Shares the storage database connection/thread with StorageManager.
class BookmarkManager : public QObject {
    Q_OBJECT
public:
    explicit BookmarkManager(QSqlDatabase db, QObject *parent = nullptr);

    qint64 createGroup(const QString &name, qint64 parentId = 0, const QString &color = QString(),
                       const QString &icon = QString());
    bool updateGroup(qint64 id, const QString &name, const QString &color, const QString &icon);
    // Deletes a group; child groups are re-parented to the deleted group's parent.
    bool deleteGroup(qint64 id);
    // Moves a group under newParentId (0 = top level); rejects cycles.
    bool moveGroup(qint64 id, qint64 newParentId);

    QVector<BookmarkGroup> groups() const;
    std::optional<BookmarkGroup> group(qint64 id) const;

    bool assignEntry(qint64 entryId, qint64 groupId);
    bool removeFromGroup(qint64 entryId, qint64 groupId);
    QList<qint64> entryIdsForGroup(qint64 groupId) const;
    QList<qint64> groupIdsForEntry(qint64 entryId) const;
    int entryCount(qint64 groupId) const;

    bool isDescendantOf(qint64 childId, qint64 ancestorId) const;

signals:
    void groupsChanged();
    void membershipChanged(qint64 entryId);

private:
    // Case-insensitive sibling check used to keep group names unique.
    bool groupNameExists(const QString &name, qint64 parentId, qint64 excludeId) const;

    QSqlDatabase m_db;
};
