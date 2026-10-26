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
    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty() || parentId < 0 || (parentId > 0 && !group(parentId).has_value()))
        return 0;
    // Sibling names are unique case-insensitively, mirroring the tag table.
    if (groupNameExists(normalizedName, parentId, 0)) {
        qWarning("egoboard: createGroup: a group named \"%s\" already exists here",
                 qPrintable(normalizedName));
        return 0;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO groups (parent_id, name, color, icon) VALUES (:p, :n, :c, :i)"));
    query.bindValue(QStringLiteral(":p"), parentId > 0 ? QVariant(parentId) : QVariant());
    query.bindValue(QStringLiteral(":n"), normalizedName);
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
    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty())
        return false;
    const auto existing = group(id);
    if (!existing.has_value())
        return false;
    if (groupNameExists(normalizedName, existing->parentId, id)) {
        qWarning("egoboard: updateGroup: a group named \"%s\" already exists here",
                 qPrintable(normalizedName));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE groups SET name = :n, color = :c, icon = :i WHERE id = :id"));
    query.bindValue(QStringLiteral(":n"), normalizedName);
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
    static const QString savepoint = QStringLiteral("egoboard_delete_group");
    QSqlQuery transaction(m_db);
    if (!transaction.exec(QStringLiteral("SAVEPOINT ") + savepoint)) {
        qWarning("egoboard: deleteGroup cannot create savepoint: %s",
                 qPrintable(transaction.lastError().text()));
        return false;
    }
    const auto rollback = [this] {
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral("ROLLBACK TO SAVEPOINT egoboard_delete_group")))
            qWarning("egoboard: deleteGroup rollback failed: %s", qPrintable(query.lastError().text()));
        if (!query.exec(QStringLiteral("RELEASE SAVEPOINT egoboard_delete_group")))
            qWarning("egoboard: deleteGroup savepoint release failed: %s",
                     qPrintable(query.lastError().text()));
    };

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT parent_id FROM groups WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next()) {
        rollback();
        return false;
    }
    const qint64 parentId = query.value(0).toLongLong();
    query.finish();

    query.prepare(QStringLiteral("UPDATE groups SET parent_id = :p WHERE parent_id = :id"));
    query.bindValue(QStringLiteral(":p"), parentId > 0 ? QVariant(parentId) : QVariant());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        rollback();
        return false;
    }
    query.finish();
    query.prepare(QStringLiteral("DELETE FROM groups WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    const bool ok = query.exec() && query.numRowsAffected() > 0;
    query.finish();
    if (!ok || !transaction.exec(QStringLiteral("RELEASE SAVEPOINT ") + savepoint)) {
        rollback();
        return false;
    }
    emit groupsChanged();
    return true;
}

bool BookmarkManager::moveGroup(qint64 id, qint64 newParentId)
{
    const auto existing = group(id);
    if (!existing.has_value() || newParentId < 0 || id == newParentId)
        return false;
    if (newParentId > 0) {
        if (!group(newParentId).has_value() || isDescendantOf(newParentId, id))
            return false; // missing parent or would create a cycle
    }
    if (groupNameExists(existing->name, newParentId, id))
        return false;
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
    const bool inserted = query.numRowsAffected() > 0;
    emit membershipChanged(entryId);
    if (inserted)
        emit membershipCountChanged(groupId, 1);
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
    const bool removed = query.numRowsAffected() > 0;
    emit membershipChanged(entryId);
    if (removed)
        emit membershipCountChanged(groupId, -1);
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

QList<qint64> BookmarkManager::entryIdsForGroups(const QSet<qint64> &groupIds) const
{
    static constexpr int kGroupQueryBatchSize = 500; // below SQLite's minimum bind-variable cap
    const QList<qint64> groupIdList = groupIds.values();
    QList<qint64> entryIds;
    QSet<qint64> seen;
    seen.reserve(groupIds.size());
    for (int offset = 0; offset < groupIdList.size(); offset += kGroupQueryBatchSize) {
        const int count = qMin(kGroupQueryBatchSize, groupIdList.size() - offset);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i)
            placeholders.append(QStringLiteral("?"));
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("SELECT entry_id FROM entry_groups WHERE group_id IN (%1)")
                          .arg(placeholders.join(QLatin1Char(','))));
        for (int i = 0; i < count; ++i)
            query.bindValue(i, groupIdList.at(offset + i));
        if (!query.exec()) {
            qWarning("egoboard: cannot fetch entries for groups: %s",
                     qPrintable(query.lastError().text()));
            continue;
        }
        while (query.next()) {
            const qint64 entryId = query.value(0).toLongLong();
            if (entryId != 0 && !seen.contains(entryId)) {
                seen.insert(entryId);
                entryIds.append(entryId);
            }
        }
    }
    return entryIds;
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

bool BookmarkManager::groupIdsForEntries(
    const QList<qint64> &entryIds, QHash<qint64, QList<qint64>> *groupsByEntry) const
{
    if (!groupsByEntry)
        return false;
    groupsByEntry->clear();
    static constexpr int kEntryQueryBatchSize = 500;
    for (int offset = 0; offset < entryIds.size(); offset += kEntryQueryBatchSize) {
        const int count = qMin(kEntryQueryBatchSize, entryIds.size() - offset);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i)
            placeholders.append(QStringLiteral("?"));
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT entry_id, group_id FROM entry_groups WHERE entry_id IN (%1)"
            " ORDER BY entry_id, group_id")
                          .arg(placeholders.join(QLatin1Char(','))));
        for (int i = 0; i < count; ++i)
            query.bindValue(i, entryIds.at(offset + i));
        if (!query.exec()) {
            qWarning("egoboard: cannot fetch groups for entries: %s",
                     qPrintable(query.lastError().text()));
            return false;
        }
        while (query.next())
            (*groupsByEntry)[query.value(0).toLongLong()].append(query.value(1).toLongLong());
    }
    return true;
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

QHash<qint64, int> BookmarkManager::entryCounts() const
{
    QHash<qint64, int> counts;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT group_id, COUNT(*) FROM entry_groups GROUP BY group_id"))) {
        qWarning("egoboard: cannot fetch group entry counts: %s",
                 qPrintable(query.lastError().text()));
        return counts;
    }
    while (query.next())
        counts.insert(query.value(0).toLongLong(), query.value(1).toInt());
    return counts;
}

bool BookmarkManager::groupNameExists(const QString &name, qint64 parentId, qint64 excludeId) const
{
    QSqlQuery query(m_db);
    if (parentId > 0) {
        query.prepare(QStringLiteral(
            "SELECT 1 FROM groups WHERE parent_id = :p AND name = :n COLLATE NOCASE AND id != :id"));
        query.bindValue(QStringLiteral(":p"), parentId);
    } else {
        query.prepare(QStringLiteral(
            "SELECT 1 FROM groups WHERE parent_id IS NULL AND name = :n COLLATE NOCASE AND id != :id"));
    }
    query.bindValue(QStringLiteral(":n"), name);
    query.bindValue(QStringLiteral(":id"), excludeId);
    return query.exec() && query.next();
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
