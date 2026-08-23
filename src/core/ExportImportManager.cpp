#include "ExportImportManager.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

namespace {

ContentType typeFromTag(const QString &tag)
{
    if (tag == QLatin1String("richtext"))
        return ContentType::RichText;
    if (tag == QLatin1String("image"))
        return ContentType::Image;
    if (tag == QLatin1String("files"))
        return ContentType::Files;
    return ContentType::Text;
}

QSet<qint64> descendantGroupIds(const QVector<BookmarkGroup> &allGroups, qint64 rootId)
{
    QSet<qint64> result;
    result.insert(rootId);
    bool grew = true;
    while (grew) {
        grew = false;
        for (const BookmarkGroup &group : allGroups) {
            if (result.contains(group.parentId) && !result.contains(group.id)) {
                result.insert(group.id);
                grew = true;
            }
        }
    }
    return result;
}

// Stable group identity across machines: the path of names from the root.
QString groupPath(const QVector<BookmarkGroup> &allGroups, qint64 groupId)
{
    QStringList names;
    qint64 current = groupId;
    QSet<qint64> visited;
    while (current != 0 && !visited.contains(current)) {
        visited.insert(current);
        const auto it = std::find_if(allGroups.cbegin(), allGroups.cend(),
                                     [current](const BookmarkGroup &g) { return g.id == current; });
        if (it == allGroups.cend())
            break;
        names.prepend(it->name);
        current = it->parentId;
    }
    return names.join(QStringLiteral("/"));
}

std::optional<qint64> findByHash(QSqlDatabase db, const QByteArray &hash)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM entries WHERE content_hash = :h LIMIT 1"));
    query.bindValue(QStringLiteral(":h"), hash);
    if (query.exec() && query.next())
        return query.value(0).toLongLong();
    return std::nullopt;
}

} // namespace

ExportImportManager::ExportImportManager(StorageManager *storage, BookmarkManager *bookmarks,
                                         QObject *parent)
    : QObject(parent)
    , m_storage(storage)
    , m_bookmarks(bookmarks)
{
}

bool ExportImportManager::exportToFile(const ExportRequest &request, QString *error)
{
    const QVector<BookmarkGroup> allGroups = m_bookmarks->groups();
    QSet<qint64> exportedGroupIds;
    for (const BookmarkGroup &group : allGroups)
        exportedGroupIds.insert(group.id);

    // --- gather entries -----------------------------------------------------
    FilterSpec filter;
    QSet<qint64> entryIdSet; // used for group scopes
    QVector<ClipboardRecord> entries;
    if (request.scope == Scope::Everything) {
        entries = m_storage->fetchAllFull(filter);
    } else if (request.scope == Scope::PinnedOnly) {
        filter.pinnedOnly = true;
        entries = m_storage->fetchAllFull(filter);
    } else {
        exportedGroupIds =
            descendantGroupIds(allGroups, request.groupId);
        for (const qint64 gid : exportedGroupIds) {
            const QList<qint64> ids = m_bookmarks->entryIdsForGroup(gid);
            for (const qint64 id : ids) {
                if (entryIdSet.contains(id))
                    continue;
                entryIdSet.insert(id);
                ClipboardRecord record;
                if (m_storage->fetchFull(id, &record))
                    entries.append(record);
            }
        }
    }

    // --- memberships --------------------------------------------------------
    QJsonArray memberships;
    if (request.scope != Scope::PinnedOnly) {
        for (const ClipboardRecord &record : entries) {
            const QList<qint64> groupIds = m_bookmarks->groupIdsForEntry(record.id);
            for (const qint64 gid : groupIds) {
                if (!exportedGroupIds.contains(gid))
                    continue;
                QJsonObject membership;
                membership.insert(QStringLiteral("entryHash"), QString::fromLatin1(record.hash));
                membership.insert(QStringLiteral("groupId"), double(gid));
                memberships.append(membership);
            }
        }
    } else {
        for (const ClipboardRecord &record : entries) {
            for (const qint64 gid : m_bookmarks->groupIdsForEntry(record.id)) {
                QJsonObject membership;
                membership.insert(QStringLiteral("entryHash"), QString::fromLatin1(record.hash));
                membership.insert(QStringLiteral("groupId"), double(gid));
                memberships.append(membership);
            }
        }
    }

    // --- entries ------------------------------------------------------------
    QJsonArray entryArray;
    for (const ClipboardRecord &record : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("hash"), QString::fromLatin1(record.hash));
        object.insert(QStringLiteral("timestamp"), double(record.timestamp));
        object.insert(QStringLiteral("type"), QLatin1String(contentTypeTag(record.type)));
        object.insert(QStringLiteral("text"), record.textData);
        if (record.hasBlob)
            object.insert(QStringLiteral("blob"),
                          QString::fromLatin1(record.blobData.toBase64()));
        object.insert(QStringLiteral("preview"), record.preview);
        object.insert(QStringLiteral("sizeBytes"), double(record.sizeBytes));
        object.insert(QStringLiteral("pinned"), record.pinned);
        object.insert(QStringLiteral("sensitive"), record.sensitive);
        object.insert(QStringLiteral("useCount"), record.useCount);
        object.insert(QStringLiteral("sourceApp"), record.sourceApp);
        object.insert(QStringLiteral("sourceWindow"), record.sourceWindow);
        entryArray.append(object);
    }

    // --- groups -------------------------------------------------------------
    QJsonArray groupArray;
    for (const BookmarkGroup &group : allGroups) {
        if (!exportedGroupIds.contains(group.id))
            continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), double(group.id));
        object.insert(QStringLiteral("parentId"), double(group.parentId));
        object.insert(QStringLiteral("name"), group.name);
        object.insert(QStringLiteral("color"), group.color);
        object.insert(QStringLiteral("icon"), group.icon);
        groupArray.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), exportFormatTag());
    root.insert(QStringLiteral("version"), exportFormatVersion());
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("entries"), entryArray);
    root.insert(QStringLiteral("groups"), groupArray);
    root.insert(QStringLiteral("memberships"), memberships);

    QFile file(request.path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2")
                         .arg(request.path, file.errorString());
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (error)
            *error = QObject::tr("Write to %1 failed: %2").arg(request.path, file.errorString());
        return false;
    }
    return true;
}

ExportImportManager::ImportResult
ExportImportManager::importFromFile(const QString &path, ImportMode mode)
{
    ImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = tr("Cannot read %1: %2").arg(path, file.errorString());
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = tr("%1 is not a valid Egoboard export file").arg(path);
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != exportFormatTag()) {
        result.error = tr("%1 is not a valid Egoboard export file").arg(path);
        return result;
    }
    if (root.value(QStringLiteral("version")).toInt() > exportFormatVersion()) {
        result.error = tr("Export file format version %1 is newer than supported")
                           .arg(root.value(QStringLiteral("version")).toInt());
        return result;
    }

    QSqlDatabase db = m_storage->database();

    if (mode == ImportMode::Overwrite) {
        m_storage->clearHistory(true);
        QSqlQuery wipe(db);
        wipe.exec(QStringLiteral("DELETE FROM groups")); // cascades memberships
    }

    // --- groups: map imported ids to local ids by matching full path --------
    const QJsonArray groupArray = root.value(QStringLiteral("groups")).toArray();
    QVector<BookmarkGroup> importedGroups;
    for (const auto &value : groupArray) {
        const QJsonObject object = value.toObject();
        BookmarkGroup group;
        group.id = qint64(object.value(QStringLiteral("id")).toDouble());
        group.parentId = qint64(object.value(QStringLiteral("parentId")).toDouble());
        group.name = object.value(QStringLiteral("name")).toString();
        group.color = object.value(QStringLiteral("color")).toString();
        group.icon = object.value(QStringLiteral("icon")).toString();
        if (group.id != 0 && !group.name.isEmpty())
            importedGroups.append(group);
    }

    QHash<qint64, qint64> groupIdMap; // imported id -> local id
    const auto localGroups = [this]() { return m_bookmarks->groups(); };
    const auto importGroup = [&](const BookmarkGroup &imported, qint64 parentId) {
        const QString path = groupPath(importedGroups, imported.id);
        const QVector<BookmarkGroup> existing = localGroups();
        std::optional<qint64> match;
        for (const BookmarkGroup &candidate : existing) {
            if (groupPath(existing, candidate.id) == path) {
                match = candidate.id;
                break;
            }
        }
        if (match.has_value()) {
            groupIdMap.insert(imported.id, *match);
            return;
        }
        const qint64 created =
            m_bookmarks->createGroup(imported.name, parentId, imported.color, imported.icon);
        if (created != 0) {
            groupIdMap.insert(imported.id, created);
            ++result.groupsImported;
        }
    };

    // The JSON array is name-ordered, so a child may appear before its
    // parent. Process imported groups in dependency order to preserve the
    // hierarchy regardless of serialization order.
    QSet<qint64> pendingGroupIds;
    for (const BookmarkGroup &group : importedGroups)
        pendingGroupIds.insert(group.id);

    bool madeProgress = true;
    while (!pendingGroupIds.isEmpty() && madeProgress) {
        madeProgress = false;
        for (const BookmarkGroup &imported : importedGroups) {
            if (!pendingGroupIds.contains(imported.id))
                continue;
            if (imported.parentId != 0 && pendingGroupIds.contains(imported.parentId))
                continue;

            importGroup(imported, groupIdMap.value(imported.parentId, 0));
            pendingGroupIds.remove(imported.id);
            madeProgress = true;
        }
    }

    // Malformed files may contain parent references to missing groups. Keep
    // those groups importable as top-level entries instead of dropping them.
    for (const BookmarkGroup &imported : importedGroups) {
        if (pendingGroupIds.contains(imported.id))
            importGroup(imported, 0);
    }

    // --- entries -------------------------------------------------------------
    const QJsonArray entryArray = root.value(QStringLiteral("entries")).toArray();
    QHash<QByteArray, qint64> entryIdByHash; // imported hash -> local id
    for (const auto &value : entryArray) {
        const QJsonObject object = value.toObject();
        ClipboardRecord record;
        record.hash = object.value(QStringLiteral("hash")).toString().toLatin1();
        record.timestamp = qint64(object.value(QStringLiteral("timestamp")).toDouble());
        record.type = typeFromTag(object.value(QStringLiteral("type")).toString());
        record.textData = object.value(QStringLiteral("text")).toString();
        record.blobData = QByteArray::fromBase64(
            object.value(QStringLiteral("blob")).toString().toLatin1());
        record.hasBlob = !record.blobData.isEmpty();
        record.preview = object.value(QStringLiteral("preview")).toString();
        record.sizeBytes = qint64(object.value(QStringLiteral("sizeBytes")).toDouble());
        record.pinned = object.value(QStringLiteral("pinned")).toBool(false);
        record.sensitive = object.value(QStringLiteral("sensitive")).toBool(false);
        record.useCount = object.value(QStringLiteral("useCount")).toInt();
        record.sourceApp = object.value(QStringLiteral("sourceApp")).toString();
        record.sourceWindow = object.value(QStringLiteral("sourceWindow")).toString();
        if (record.hash.isEmpty())
            continue;

        const auto existingId = findByHash(db, record.hash);
        if (existingId.has_value()) {
            entryIdByHash.insert(record.hash, *existingId);
            if (mode == ImportMode::SkipDuplicates) {
                ++result.entriesSkipped;
                continue;
            }
            if (mode == ImportMode::Merge) {
                ClipboardRecord existing;
                if (m_storage->fetchFull(*existingId, &existing)) {
                    QSqlQuery update(db);
                    update.prepare(QStringLiteral(
                        "UPDATE entries SET timestamp_ms = :ts, pinned = MAX(pinned, :p),"
                        " use_count = MAX(use_count, :uc), sensitive = MAX(sensitive, :s)"
                        " WHERE id = :id"));
                    update.bindValue(
                        QStringLiteral(":ts"),
                        qMax(existing.timestamp, record.timestamp));
                    update.bindValue(QStringLiteral(":p"), record.pinned ? 1 : 0);
                    update.bindValue(QStringLiteral(":uc"), record.useCount);
                    update.bindValue(QStringLiteral(":s"), record.sensitive ? 1 : 0);
                    update.bindValue(QStringLiteral(":id"), *existingId);
                    update.exec();
                }
                ++result.entriesMerged;
                continue;
            }
            // Overwrite already wiped; a hash hit here means duplicate inside file.
            ++result.entriesSkipped;
            continue;
        }

        record.id = 0;
        const qint64 insertedId = m_storage->insertOrUpdate(record);
        if (insertedId != 0) {
            entryIdByHash.insert(record.hash, insertedId);
            ++result.entriesImported;
            // insertOrUpdate resets use_count; restore imported value.
            QSqlQuery fixCount(db);
            fixCount.prepare(QStringLiteral("UPDATE entries SET use_count = :uc WHERE id = :id"));
            fixCount.bindValue(QStringLiteral(":uc"), record.useCount);
            fixCount.bindValue(QStringLiteral(":id"), insertedId);
            fixCount.exec();
        }
    }

    // --- memberships ----------------------------------------------------------
    const QJsonArray membershipArray = root.value(QStringLiteral("memberships")).toArray();
    for (const auto &value : membershipArray) {
        const QJsonObject object = value.toObject();
        const QByteArray hash = object.value(QStringLiteral("entryHash")).toString().toLatin1();
        const qint64 importedGroupId = qint64(object.value(QStringLiteral("groupId")).toDouble());
        const qint64 localGroupId = groupIdMap.value(importedGroupId, 0);
        const qint64 localEntryId = entryIdByHash.value(hash, 0);
        if (localGroupId != 0 && localEntryId != 0)
            m_bookmarks->assignEntry(localEntryId, localGroupId);
    }

    emit m_storage->storageReset(); // coarser but correct: let views reload
    result.ok = true;
    return result;
}
