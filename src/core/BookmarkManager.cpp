#include "BookmarkManager.h"

#include <QSqlError>
#include <QSqlQuery>

BookmarkManager::BookmarkManager(QSqlDatabase db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

qint64 BookmarkManager::createGroup(const QString &name, qint64 parentId, const QString &color,
                                    const QString &icon)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO groups (parent_id, name, color, icon) VALUES (:p, :n, :c, :i)"));
    query.bindValue(QStringLiteral(":p"), parentId > 0 ? QVariant(parentId) : QVariant());
    query.bindValue(QStringLiteral(":n"), name);
    query.bindValue(QStringLiteral(":c"), color);
    query.bindValue(QStringLiteral(":i"), icon);
    if (!query.exec()) {
        qWarning("egoboard: createGroup failed: %s", qPrintable(query.lastError().text()));
        return 0;
    }
    emit groupsChanged();
    return query.lastInsertId().toLongLong();
}

bool BookmarkManager::updateGroup(qint64 id, const QString &name, const QString &color,
                                  const QString &icon)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE groups SET name = :n, color = :c, icon = :i WHERE id = :id"));
    query.bindValue(QStringLiteral(":n"), name);
    query.bindValue(QStringLiteral(":c"), color);
    query.bindValue(QStringLiteral(":i"), icon);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || query.numRowsAffected() == 0)
        return false;
    emit groupsChanged();
    return true;
}

bool BookmarkManager::deleteGroup(qint64 id)
{
    if (!m_db.transaction())
        qWarning("egoboard: deleteGroup cannot start transaction: %s",
                 qPrintable(m_db.lastError().text()));
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT parent_id FROM groups WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next()) {
        m_db.rollback();
        return false;
    }
    const qint64 parentId = query.value(0).toLongLong();

    query.prepare(QStringLiteral("UPDATE groups SET parent_id = :p WHERE parent_id = :id"));
    query.bindValue(QStringLiteral(":p"), parentId > 0 ? QVariant(parentId) : QVariant());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        m_db.rollback();
        return false;
    }
    query.prepare(QStringLiteral("DELETE FROM groups WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    const bool ok = query.exec() && query.numRowsAffected() > 0;
    m_db.commit();
    if (ok)
        emit groupsChanged();
    return ok;
}

bool BookmarkManager::moveGroup(qint64 id, qint64 newParentId)
{
    if (id == newParentId)
        return false;
    if (newParentId != 0 && isDescendantOf(newParentId, id))
        return false; // would create a cycle
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE groups SET parent_id = :p WHERE id = :id"));
    query.bindValue(QStringLiteral(":p"), newParentId > 0 ? QVariant(newParentId) : QVariant());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || query.numRowsAffected() == 0)
        return false;
    emit groupsChanged();
    return true;
}

QVector<BookmarkGroup> BookmarkManager::groups() const
{
    QVector<BookmarkGroup> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, parent_id, name, color, icon FROM groups ORDER BY name COLLATE NOCASE"));
    if (!query.exec()) {
        qWarning("egoboard: groups() failed: %s", qPrintable(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        BookmarkGroup group;
        group.id = query.value(0).toLongLong();
        group.parentId = query.value(1).toLongLong();
        group.name = query.value(2).toString();
        group.color = query.value(3).toString();
        group.icon = query.value(4).toString();
        result.append(group);
    }
    return result;
}

std::optional<BookmarkGroup> BookmarkManager::group(qint64 id) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, parent_id, name, color, icon FROM groups WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return std::nullopt;
    BookmarkGroup group;
    group.id = query.value(0).toLongLong();
    group.parentId = query.value(1).toLongLong();
    group.name = query.value(2).toString();
    group.color = query.value(3).toString();
    group.icon = query.value(4).toString();
    return group;
}

bool BookmarkManager::assignEntry(qint64 entryId, qint64 groupId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO entry_groups (entry_id, group_id) VALUES (:e, :g)"));
    query.bindValue(QStringLiteral(":e"), entryId);
    query.bindValue(QStringLiteral(":g"), groupId);
    if (!query.exec())
        return false;
    emit membershipChanged(entryId);
    return true;
}

bool BookmarkManager::removeFromGroup(qint64 entryId, qint64 groupId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM entry_groups WHERE entry_id = :e AND group_id = :g"));
    query.bindValue(QStringLiteral(":e"), entryId);
    query.bindValue(QStringLiteral(":g"), groupId);
    if (!query.exec())
        return false;
    emit membershipChanged(entryId);
    return true;
}

QList<qint64> BookmarkManager::entryIdsForGroup(qint64 groupId) const
{
    QList<qint64> ids;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT entry_id FROM entry_groups WHERE group_id = :g"));
    query.bindValue(QStringLiteral(":g"), groupId);
    if (query.exec()) {
        while (query.next())
            ids.append(query.value(0).toLongLong());
    }
    return ids;
}

QList<qint64> BookmarkManager::groupIdsForEntry(qint64 entryId) const
{
    QList<qint64> ids;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT group_id FROM entry_groups WHERE entry_id = :e"));
    query.bindValue(QStringLiteral(":e"), entryId);
    if (query.exec()) {
        while (query.next())
            ids.append(query.value(0).toLongLong());
    }
    return ids;
}

int BookmarkManager::entryCount(qint64 groupId) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM entry_groups WHERE group_id = :g"));
    query.bindValue(QStringLiteral(":g"), groupId);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

bool BookmarkManager::isDescendantOf(qint64 childId, qint64 ancestorId) const
{
    qint64 current = childId;
    // The tree is small; a bounded walk with a visited-guard is plenty.
    QSet<qint64> visited;
    while (current != 0) {
        if (current == ancestorId)
            return true;
        if (visited.contains(current))
            return false; // defensive: corrupt cycle in DB
        visited.insert(current);
        const auto parent = group(current);
        if (!parent.has_value())
            return false;
        current = parent->parentId;
    }
    return false;
}
