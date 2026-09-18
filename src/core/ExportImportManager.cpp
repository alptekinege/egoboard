#include "ExportImportManager.h"

#include "ContentType.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>

namespace {

// Automatic backups: egoboard-backup-20260918-034512.json (name order is
// chronological, which is what pruning relies on).
constexpr auto kBackupPrefix = "egoboard-backup-";

// One list line for an imported entry, matching the capture previews.
QString singleLinePreview(const QString &text, int maxLength = 180)
{
    QString line = text.simplified();
    if (line.size() > maxLength)
        line = line.left(maxLength - 1) + QChar(0x2026);
    return line;
}

// --- helpers for the reading formats (CSV / Markdown / HTML) -----------------

bool writeTextFile(const QString &path, const QString &content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray payload = content.toUtf8();
    if (file.write(payload) != payload.size()) {
        if (error)
            *error = QObject::tr("Write to %1 failed: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

// RFC 4180: quote when the field holds a delimiter, a quote or a line break.
QString csvField(const QString &value)
{
    if (!value.contains(QLatin1Char(',')) && !value.contains(QLatin1Char('"'))
        && !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r')))
        return value;
    QString quoted = value;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString htmlEscaped(const QString &value)
{
    QString out = value;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

// A fence long enough to survive any backtick run inside the block.
QString markdownFence(const QString &text)
{
    int longest = 0;
    int current = 0;
    for (const QChar c : text) {
        current = c == QLatin1Char('`') ? current + 1 : 0;
        longest = qMax(longest, current);
    }
    return QString(QLatin1Char('`')).repeated(qMax(3, longest + 1));
}

QStringList backupFiles(const QString &folder)
{
    QDir dir(folder);
    const QStringList names = dir.entryList(
        {QString::fromLatin1(kBackupPrefix) + QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QStringList paths;
    paths.reserve(names.size());
    for (const QString &name : names)
        paths << dir.absoluteFilePath(name);
    return paths; // ascending by name == oldest first
}

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
    query.bindValue(QStringLiteral(":h"), QString::fromLatin1(hash));
    if (query.exec() && query.next())
        return query.value(0).toLongLong();
    return std::nullopt;
}

} // namespace

ExportImportManager::ExportImportManager(StorageManager *storage, BookmarkManager *bookmarks,
                                         SnippetManager *snippets, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
    , m_bookmarks(bookmarks)
    , m_snippets(snippets)
{
}

QString ExportImportManager::formatId(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Json:
        return QStringLiteral("json");
    case ExportFormat::Markdown:
        return QStringLiteral("markdown");
    case ExportFormat::Csv:
        return QStringLiteral("csv");
    case ExportFormat::Html:
        return QStringLiteral("html");
    }
    return QStringLiteral("json");
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

    // --- reading formats stop here (entries only, no re-import) ---------------
    if (request.format == ExportFormat::Csv)
        return writeCsvExport(request, entries, error);
    if (request.format == ExportFormat::Markdown)
        return writeMarkdownExport(request, entries, error);
    if (request.format == ExportFormat::Html)
        return writeHtmlExport(request, entries, error);

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
        if (!record.ocrText.isEmpty())
            object.insert(QStringLiteral("ocrText"), record.ocrText);
        const QStringList tags = m_storage->tagsForEntry(record.id);
        if (!tags.isEmpty()) {
            QJsonArray tagArray;
            for (const QString &tag : tags)
                tagArray.append(tag);
            object.insert(QStringLiteral("tags"), tagArray);
        }
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

    // --- snippet library ----------------------------------------------------
    QJsonArray snippetArray;
    if (m_snippets) {
        const QVector<Snippet> snippets = m_snippets->snippets();
        for (const Snippet &snippet : snippets) {
            QJsonObject object;
            object.insert(QStringLiteral("name"), snippet.name);
            object.insert(QStringLiteral("template"), snippet.templateText);
            object.insert(QStringLiteral("shortcut"), snippet.shortcut);
            object.insert(QStringLiteral("createdMs"), double(snippet.createdMs));
            snippetArray.append(object);
        }
    }

    // --- saved searches -----------------------------------------------------
    QJsonArray searchArray;
    const QList<SavedSearch> searches = m_storage->savedSearches();
    for (const SavedSearch &search : searches) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), search.name);
        object.insert(QStringLiteral("filter"), search.filter.toJson());
        searchArray.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), exportFormatTag());
    root.insert(QStringLiteral("version"), exportFormatVersion());
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("entries"), entryArray);
    root.insert(QStringLiteral("groups"), groupArray);
    root.insert(QStringLiteral("memberships"), memberships);
    root.insert(QStringLiteral("snippets"), snippetArray);
    root.insert(QStringLiteral("savedSearches"), searchArray);

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
    if (!db.isOpen()) {
        result.error = tr("Database connection is not open");
        return result;
    }

    // One transaction and no per-row signals for the whole import: the single
    // storageReset() at the end tells every view to reload.
    const bool bulk = m_storage->beginBulk();

    if (mode == ImportMode::Overwrite) {
        // A self-contained backup replaces every piece of user data it carries.
        m_storage->clearHistory(true);
        QSqlQuery wipe(db);
        wipe.exec(QStringLiteral("DELETE FROM groups")); // cascades memberships
        wipe.exec(QStringLiteral("DELETE FROM tags")); // entry links already cascaded
        wipe.exec(QStringLiteral("DELETE FROM snippets"));
        wipe.exec(QStringLiteral("DELETE FROM saved_searches"));
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
    const auto applyImportedTags = [this, &result](qint64 entryId, const QStringList &tags) {
        for (const QString &tag : tags) {
            if (m_storage->addTag(entryId, tag))
                ++result.tagsImported;
        }
    };

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
        record.ocrText = object.value(QStringLiteral("ocrText")).toString();
        QStringList tagNames;
        const QJsonArray tagArray = object.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &tagValue : tagArray) {
            const QString tag = tagValue.toString().trimmed();
            if (!tag.isEmpty())
                tagNames.append(tag);
        }
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
                    if (!record.ocrText.isEmpty() && existing.ocrText.isEmpty())
                        m_storage->setOcrText(*existingId, record.ocrText);
                }
                applyImportedTags(*existingId, tagNames);
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
            if (!record.ocrText.isEmpty())
                m_storage->setOcrText(insertedId, record.ocrText);
            applyImportedTags(insertedId, tagNames);
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

    // --- snippet library ------------------------------------------------------
    // Snippets have no stable identity across machines, so the name is the key:
    // an existing snippet with the same name is left untouched (Overwrite mode
    // started from an empty table and re-creates everything).
    const QJsonArray snippetArray = root.value(QStringLiteral("snippets")).toArray();
    QSet<QString> importedSnippetNames;
    for (const auto &value : snippetArray) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        const QString plainTemplate = object.value(QStringLiteral("template")).toString();
        if (name.isEmpty() || plainTemplate.isEmpty())
            continue;
        const QString key = name.toCaseFolded();
        if (importedSnippetNames.contains(key))
            continue;

        QSqlQuery probe(db);
        probe.prepare(QStringLiteral("SELECT id FROM snippets WHERE name = :name LIMIT 1"));
        probe.bindValue(QStringLiteral(":name"), name);
        if (probe.exec() && probe.next()) {
            importedSnippetNames.insert(key);
            continue;
        }

        QSqlQuery insert(db);
        insert.prepare(QStringLiteral(
            "INSERT INTO snippets (name, template, shortcut, created_ms)"
            " VALUES (:name, :template, :shortcut, :created)"));
        insert.bindValue(QStringLiteral(":name"), name);
        insert.bindValue(QStringLiteral(":template"), plainTemplate);
        insert.bindValue(QStringLiteral(":shortcut"),
                         object.value(QStringLiteral("shortcut")).toString());
        insert.bindValue(QStringLiteral(":created"),
                         qint64(object.value(QStringLiteral("createdMs")).toDouble()));
        if (insert.exec()) {
            importedSnippetNames.insert(key);
            ++result.snippetsImported;
        } else {
            qWarning("egoboard: snippet import failed: %s",
                     qPrintable(insert.lastError().text()));
        }
    }

    // --- saved searches -------------------------------------------------------
    const QJsonArray searchArray = root.value(QStringLiteral("savedSearches")).toArray();
    for (const auto &value : searchArray) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;
        const FilterSpec filter =
            FilterSpec::fromJson(object.value(QStringLiteral("filter")).toObject());
        if (m_storage->addSavedSearch(name, filter) != 0)
            ++result.savedSearchesImported;
    }

    if (bulk)
        m_storage->endBulk(true);

    emit m_storage->storageReset(); // one reload for the whole import
    result.ok = true;
    return result;
}

bool ExportImportManager::writeCsvExport(const ExportRequest &request,
                                         const QVector<ClipboardRecord> &entries, QString *error) const
{
    QString out;
    out.reserve(entries.size() * 160);
    out += QStringLiteral(
        "timestamp,type,source_app,source_window,pinned,sensitive,use_count,tags,text\n");
    for (const ClipboardRecord &record : entries) {
        const QString text = record.textData.isEmpty() ? record.preview : record.textData;
        const QString tags = m_storage->tagsForEntry(record.id).join(QStringLiteral("; "));
        out += csvField(QDateTime::fromMSecsSinceEpoch(record.timestamp).toString(Qt::ISODateWithMs));
        out += QLatin1Char(',');
        out += csvField(QString::fromLatin1(contentTypeTag(record.type)));
        out += QLatin1Char(',');
        out += csvField(record.sourceApp);
        out += QLatin1Char(',');
        out += csvField(record.sourceWindow);
        out += QLatin1Char(',');
        out += record.pinned ? QLatin1Char('1') : QLatin1Char('0');
        out += QLatin1Char(',');
        out += record.sensitive ? QLatin1Char('1') : QLatin1Char('0');
        out += QLatin1Char(',');
        out += QString::number(record.useCount);
        out += QLatin1Char(',');
        out += csvField(tags);
        out += QLatin1Char(',');
        out += csvField(text);
        out += QLatin1Char('\n');
    }
    return writeTextFile(request.path, out, error);
}

bool ExportImportManager::writeMarkdownExport(const ExportRequest &request,
                                              const QVector<ClipboardRecord> &entries,
                                              QString *error) const
{
    QString out;
    out += QStringLiteral("# Egoboard history\n\n");
    out += tr("Exported %1 — %n entry/entries.", nullptr, entries.size())
               .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    out += QStringLiteral("\n");
    for (const ClipboardRecord &record : entries) {
        out += QStringLiteral("\n## %1 · %2")
                   .arg(QDateTime::fromMSecsSinceEpoch(record.timestamp)
                            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                        QString::fromLatin1(contentTypeTag(record.type)));
        if (!record.sourceApp.isEmpty())
            out += QStringLiteral(" · ") + record.sourceApp;
        if (record.pinned)
            out += QStringLiteral(" · pinned");
        if (record.sensitive)
            out += QStringLiteral(" · sensitive");
        out += QStringLiteral("\n");
        const QStringList tags = m_storage->tagsForEntry(record.id);
        if (!tags.isEmpty())
            out += tr("Tags: %1\n").arg(tags.join(QStringLiteral(", ")));
        if (record.sourceWindow.isEmpty() == false)
            out += tr("Window: %1\n").arg(record.sourceWindow);
        const QString text = record.textData.isEmpty() ? record.preview : record.textData;
        const QString fence = markdownFence(text);
        out += QStringLiteral("\n") + fence + QStringLiteral("\n") + text + QStringLiteral("\n")
               + fence + QStringLiteral("\n");
    }
    return writeTextFile(request.path, out, error);
}

bool ExportImportManager::writeHtmlExport(const ExportRequest &request,
                                          const QVector<ClipboardRecord> &entries, QString *error) const
{
    QString out;
    out += QStringLiteral(
        "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
        "<title>Egoboard history</title>\n"
        "<style>body{font-family:sans-serif;margin:2rem;}"
        "table{border-collapse:collapse;width:100%;}"
        "th,td{border:1px solid #bbb;padding:4px 8px;vertical-align:top;text-align:left;}"
        "td.text{white-space:pre-wrap;max-width:60ch;}"
        "tr:nth-child(even){background:#f6f6f6;}</style></head>\n<body>\n");
    out += QStringLiteral("<h1>Egoboard history</h1>\n<p>")
           + htmlEscaped(tr("Exported %1 — %n entry/entries.", nullptr, entries.size())
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate)))
           + QStringLiteral("</p>\n<table>\n<thead><tr>");
    const QStringList headers{tr("Time"), tr("Type"), tr("Source"), tr("Tags"), tr("Text")};
    for (const QString &header : headers)
        out += QStringLiteral("<th>") + htmlEscaped(header) + QStringLiteral("</th>");
    out += QStringLiteral("</tr></thead>\n<tbody>\n");
    for (const ClipboardRecord &record : entries) {
        const QString text = record.textData.isEmpty() ? record.preview : record.textData;
        QString source = record.sourceApp;
        if (!record.sourceWindow.isEmpty()) {
            source += source.isEmpty() ? record.sourceWindow
                                       : QStringLiteral(" — ") + record.sourceWindow;
        }
        QStringList flags;
        if (record.pinned)
            flags << tr("pinned");
        if (record.sensitive)
            flags << tr("sensitive");
        if (!flags.isEmpty())
            source += QStringLiteral(" (") + flags.join(QStringLiteral(", ")) + QLatin1Char(')');

        out += QStringLiteral("<tr><td>")
               + htmlEscaped(QDateTime::fromMSecsSinceEpoch(record.timestamp)
                                 .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
               + QStringLiteral("</td><td>")
               + htmlEscaped(QString::fromLatin1(contentTypeTag(record.type)))
               + QStringLiteral("</td><td>") + htmlEscaped(source) + QStringLiteral("</td><td>")
               + htmlEscaped(m_storage->tagsForEntry(record.id).join(QStringLiteral(", ")))
               + QStringLiteral("</td><td class=\"text\">") + htmlEscaped(text)
               + QStringLiteral("</td></tr>\n");
    }
    out += QStringLiteral("</tbody></table>\n</body></html>\n");
    return writeTextFile(request.path, out, error);
}

QString ExportImportManager::defaultKlipperPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/klipper/history3.sqlite");
}

ExportImportManager::ImportResult ExportImportManager::importKlipperHistory(const QString &databasePath)
{
    ImportResult result;
    if (!QFile::exists(databasePath)) {
        result.error = tr("Klipper history not found: %1").arg(databasePath);
        return result;
    }

    // Read-only: Klipper may be running and holding its own connection.
    static std::atomic_int connectionCounter{0};
    const QString connectionName =
        QStringLiteral("egoboard-klipper-%1").arg(connectionCounter.fetch_add(1));
    QVector<ClipboardRecord> records;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            result.error = tr("Cannot open %1: %2").arg(databasePath, db.lastError().text());
        } else {
            // The KF6 Klipper schema: main(uuid, added_time, last_used_time,
            // mimetypes, text, starred). Anything else is not a Klipper file.
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral(
                    "SELECT text, added_time, last_used_time, starred FROM main"))) {
                result.error = tr("%1 is not a Klipper history database (%2)")
                                   .arg(databasePath, query.lastError().text());
            } else {
                while (query.next()) {
                    const QString text = query.value(0).toString();
                    if (text.trimmed().isEmpty()) {
                        ++result.entriesSkipped; // image items carry no text
                        continue;
                    }
                    double added = query.value(1).toDouble();
                    const double used = query.value(2).toDouble();
                    if (added <= 0)
                        added = used;

                    ClipboardRecord record;
                    record.type = ContentType::Text;
                    record.textData = text;
                    record.preview = singleLinePreview(text);
                    record.sizeBytes = text.toUtf8().size();
                    record.timestamp = qMax<qint64>(1, qint64(added * 1000.0));
                    record.sourceApp = QStringLiteral("klipper");
                    record.pinned = query.value(3).toBool();
                    record.hash = QCryptographicHash::hash(
                                      QByteArray(contentTypeTag(record.type)) + '\0'
                                          + text.toUtf8(),
                                      QCryptographicHash::Sha256)
                                      .toHex();
                    records.append(record);
                }
            }
            db.close();
        }
        db = QSqlDatabase(); // release the handle before removing the connection
    }
    QSqlDatabase::removeDatabase(connectionName);
    if (!result.error.isEmpty())
        return result;

    // One transaction and one refresh for the whole import.
    const bool bulk = m_storage->beginBulk();
    for (const ClipboardRecord &record : records) {
        bool updatedExisting = false;
        if (m_storage->insertOrUpdate(record, &updatedExisting) == 0) {
            ++result.entriesSkipped;
            continue;
        }
        if (updatedExisting)
            ++result.entriesMerged;
        else
            ++result.entriesImported;
    }
    if (bulk)
        m_storage->endBulk(true);

    emit m_storage->storageReset();
    result.ok = true;
    return result;
}

QStringList ExportImportManager::listBackups(const QString &folder)
{
    QStringList newestFirst = backupFiles(folder);
    std::reverse(newestFirst.begin(), newestFirst.end());
    return newestFirst;
}

int ExportImportManager::pruneBackups(const QString &folder, int keep)
{
    if (keep <= 0)
        return 0; // 0 = keep everything
    const QStringList files = backupFiles(folder); // oldest first
    if (files.size() <= keep)
        return 0;
    int removed = 0;
    for (int i = 0; i < files.size() - keep; ++i) {
        if (QFile::remove(files.at(i)))
            ++removed;
    }
    return removed;
}

ExportImportManager::BackupResult ExportImportManager::writeBackup(const QString &folder, int keep)
{
    BackupResult result;
    if (folder.trimmed().isEmpty()) {
        result.error = tr("No backup folder is configured.");
        return result;
    }
    QDir dir(folder);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        result.error = tr("Cannot create the backup folder %1.").arg(folder);
        return result;
    }

    // Millisecond precision plus a zero-padded sequence keeps name order
    // chronological even when several backups land in the same millisecond.
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QString path;
    for (int sequence = 1;; ++sequence) {
        path = dir.filePath(QString::fromLatin1(kBackupPrefix) + stamp
                            + QStringLiteral("-%1.json").arg(sequence, 3, 10, QLatin1Char('0')));
        if (!QFile::exists(path))
            break;
    }

    ExportRequest request;
    request.scope = Scope::Everything;
    request.path = path;
    QString error;
    if (!exportToFile(request, &error)) {
        result.error = error;
        return result;
    }

    result.ok = true;
    result.path = path;
    result.pruned = pruneBackups(folder, keep);
    return result;
}
