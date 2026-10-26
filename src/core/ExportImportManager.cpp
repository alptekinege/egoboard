#include "ExportImportManager.h"

#include "ContentType.h"
#include "SensitiveDataDetector.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <functional>
#include <queue>
#include <vector>

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
    file.close();
    // Exports can hold passwords: never leave them world-readable.
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
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
    QStringList names;
    const QString prefix = QString::fromLatin1(kBackupPrefix);
    for (const QString &name : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                             QDir::Name)) {
        if (!name.startsWith(prefix))
            continue;
        const QFileInfo info(dir.filePath(name));
        if (info.isSymLink())
            continue;
        if ((info.isFile() && (info.suffix() == QLatin1String("json")
                               || info.suffix().compare(QLatin1String("zip"),
                                                        Qt::CaseInsensitive) == 0))
            || (info.isDir() && info.suffix() == QLatin1String("backup")))
            names.append(name);
    }
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
    result.reserve(allGroups.size());
    result.insert(rootId);
    QHash<qint64, QVector<qint64>> childrenByParent;
    childrenByParent.reserve(allGroups.size());
    for (const BookmarkGroup &group : allGroups) {
        if (group.parentId != 0)
            childrenByParent[group.parentId].append(group.id);
    }
    QList<qint64> pending;
    pending.reserve(allGroups.size());
    if (rootId == 0) {
        for (const BookmarkGroup &group : allGroups) {
            if (group.parentId == 0 && !result.contains(group.id)) {
                result.insert(group.id);
                pending.append(group.id);
            }
        }
    } else {
        pending.append(rootId);
    }
    for (qsizetype next = 0; next < pending.size(); ++next) {
        const auto children = childrenByParent.constFind(pending.at(next));
        if (children == childrenByParent.cend())
            continue;
        for (const qint64 childId : children.value()) {
            if (result.contains(childId))
                continue; // also terminates safely if malformed input cycles
            result.insert(childId);
            pending.append(childId);
        }
    }
    return result;
}

// Stable group identity across machines: the path of names from the root.
// Build all paths in one indexed walk so imports do not rescan the full group
// vector once for every parent hop and every candidate comparison.
QHash<qint64, QString> groupPaths(const QVector<BookmarkGroup> &allGroups)
{
    QHash<qint64, const BookmarkGroup *> byId;
    byId.reserve(allGroups.size());
    for (const BookmarkGroup &group : allGroups) {
        // groupPath() historically used find_if(), so duplicate IDs resolve
        // to the first record in the input vector.
        if (!byId.contains(group.id))
            byId.insert(group.id, &group);
    }

    QHash<qint64, QString> paths;
    paths.reserve(allGroups.size());
    QSet<qint64> reusablePaths;
    reusablePaths.reserve(allGroups.size());
    for (const BookmarkGroup &group : allGroups) {
        if (paths.contains(group.id))
            continue;

        QVector<qint64> chain;
        QSet<qint64> visited;
        qint64 current = group.id;
        QString path;
        bool cycle = false;
        while (current != 0) {
            if (reusablePaths.contains(current)) {
                path = paths.value(current);
                break;
            }
            if (visited.contains(current)) {
                cycle = true;
                break;
            }
            const auto it = byId.constFind(current);
            if (it == byId.cend())
                break;
            visited.insert(current);
            chain.append(current);
            current = it.value()->parentId;
        }

        if (cycle) {
            // For malformed cycles, only the starting node's path is safe to
            // cache: another start in the same cycle stops at a different hop.
            QStringList names;
            names.reserve(chain.size());
            for (auto it = chain.crbegin(); it != chain.crend(); ++it)
                names.append(byId.value(*it)->name);
            paths.insert(group.id, names.join(QLatin1Char('/')));
            continue;
        }

        for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
            const QString &name = byId.value(*it)->name;
            path = path.isEmpty() ? name : name + QLatin1Char('/') + path;
            paths.insert(*it, path);
            reusablePaths.insert(*it);
        }
    }
    return paths;
}

// --- image export (U17) ------------------------------------------------------

// One streamed batch: small enough to keep a progress dialog responsive
// between batches and to bound memory no matter how large the history grows.
constexpr int kImageExportPageSize = 200;
// Suffix attempts before giving up (row ids make true collisions impossible;
// this only guards pathological pre-existing directory content).
constexpr int kImageExportNameAttempts = 1000;

// Deterministic, collision-safe base name: capture time + row id + content
// hash prefix. The id alone already guarantees uniqueness within a database;
// the timestamp keeps folder listings chronological and the hash prefix makes
// identical re-exports recognizable.
QString imageFileBaseName(const ClipboardRecord &record)
{
    const QString stamp = QDateTime::fromMSecsSinceEpoch(record.timestamp)
                              .toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString hash = QString::fromLatin1(record.hash.left(8));
    return QStringLiteral("egoboard-%1-%2-%3").arg(stamp).arg(record.id).arg(hash);
}

QString imageScopeName(ExportImportManager::ImageExportRequest::Scope scope)
{
    using Scope = ExportImportManager::ImageExportRequest::Scope;
    switch (scope) {
    case Scope::Selection:
        return QStringLiteral("selection");
    case Scope::CurrentFilter:
        return QStringLiteral("filter");
    case Scope::PinnedOnly:
        return QStringLiteral("pinned");
    case Scope::GroupSubtree:
        return QStringLiteral("group");
    case Scope::Everything:
        break;
    }
    return QStringLiteral("everything");
}

} // namespace

// --- CopyQ `.cpq` parsing (P2-C) ------------------------------------------------
// Mirrors upstream CopyQ `src/item/serialize.cpp` (decompressMime/idToMime,
// deserializeDataV2 + the deprecated v1 branch) and
// `src/gui/mainwindow.cpp` (exportDataV4/importDataV4 header + tab layout).
// QtCore-only: plain file read, no new dependency, never touches a running
// CopyQ instance. Encrypted v5 (password-derived QCA key) is refused with a
// precise note instead of a speculative parse.
namespace CopyQImport {

constexpr int kMaxItems = 100000; // upstream MAX_ITEMS
constexpr int kMaxTabs = 1000;
constexpr int kMaxFormatsPerItem = 256;
constexpr qint64 kMaxPayloadBytes = 32LL * 1024 * 1024;
constexpr qint64 kMaxFileBytes = 512LL * 1024 * 1024;
constexpr int kMaxTextChars = 1024 * 1024;
constexpr int kMaxBlobBytes = 10 * 1024 * 1024;

struct Tab {
    QString name;
    QVector<QVariantMap> items;
};

QString decompressMime(const QString &compressed, bool *ok)
{
    if (ok)
        *ok = false;
    if (compressed.isEmpty())
        return {};
    bool hexOk = false;
    const int id = QStringView(compressed).mid(0, 1).toInt(&hexOk, 16);
    if (!hexOk)
        return {};
    if (id == 0) {
        if (ok)
            *ok = true;
        return compressed.mid(1);
    }
    QString prefix;
    switch (id) {
    case 1: prefix = QStringLiteral("application/x-copyq-owner-window-title"); break;
    case 2: prefix = QStringLiteral("application/x-copyq-item-notes"); break;
    case 3: prefix = QStringLiteral("application/x-copyq-"); break;
    case 4: prefix = QStringLiteral("text/plain"); break;
    case 5: prefix = QStringLiteral("text/html"); break;
    case 6: prefix = QStringLiteral("text/uri-list"); break;
    case 7: prefix = QStringLiteral("image/"); break;
    case 8: prefix = QStringLiteral("text/"); break;
    case 9: prefix = QStringLiteral("application/"); break;
    case 10: prefix = QStringLiteral("audio/"); break;
    case 11: prefix = QStringLiteral("video/"); break;
    default: return {};
    }
    if (ok)
        *ok = true;
    return prefix + compressed.mid(1);
}

bool readItemV2Body(QDataStream *stream, QVariantMap *data, QString *error)
{
    qint32 mapSize = 0;
    *stream >> mapSize;
    if (stream->status() != QDataStream::Ok) {
        if (error)
            *error = QObject::tr("Corrupt CopyQ export: cannot read the item format count.");
        return false;
    }
    if (mapSize < 0 || mapSize > kMaxFormatsPerItem) {
        if (error)
            *error = QObject::tr("Corrupt CopyQ export: invalid format count %1.").arg(mapSize);
        stream->setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    for (qint32 i = 0; i < mapSize; ++i) {
        QString compressed;
        *stream >> compressed;
        bool mimeOk = false;
        const QString mime = decompressMime(compressed, &mimeOk);
        if (stream->status() != QDataStream::Ok || !mimeOk) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: invalid MIME type.");
            stream->setStatus(QDataStream::ReadCorruptData);
            return false;
        }
        bool compressFlag = false;
        *stream >> compressFlag;
        QByteArray payload;
        *stream >> payload;
        if (stream->status() != QDataStream::Ok) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: cannot read item data.");
            return false;
        }
        if (payload.size() > kMaxPayloadBytes) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: item payload exceeds the size limit.");
            stream->setStatus(QDataStream::ReadCorruptData);
            return false;
        }
        if (compressFlag) {
            const QByteArray uncompressed = qUncompress(payload);
            if (uncompressed.isEmpty()) {
                if (error)
                    *error = QObject::tr("Corrupt CopyQ export: cannot decompress item data.");
                stream->setStatus(QDataStream::ReadCorruptData);
                return false;
            }
            payload = uncompressed;
        }
        // Large CopyQ items may reference an external `.dat` file instead of
        // carrying bytes (`FILE:` prefix holds the exporter's absolute path,
        // not portable content): skip the format, keep any inline sibling.
        if (mime.startsWith(QLatin1String("FILE:")))
            continue;
        data->insert(mime, payload);
    }
    return stream->status() == QDataStream::Ok;
}

bool readOneItem(QDataStream *stream, QVariantMap *data, QString *error)
{
    qint32 marker = 0;
    *stream >> marker;
    if (stream->status() != QDataStream::Ok) {
        if (error)
            *error = QObject::tr("Corrupt CopyQ export: cannot read item.");
        return false;
    }
    if (marker == -3) {
        if (error)
            *error = QObject::tr("This CopyQ export is encrypted (CopyQ v5, password-protected); "
                                 "export it from CopyQ without a password and import the "
                                 "unencrypted file.");
        stream->setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    if (marker == -2)
        return readItemV2Body(stream, data, error);
    if (marker < 0) {
        if (error)
            *error = QObject::tr("Corrupt CopyQ export: unsupported item version %1.").arg(marker);
        stream->setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    // Deprecated v1 item: marker is the MIME count, mimes are literal and
    // payloads are qCompressed.
    if (marker > kMaxFormatsPerItem) {
        if (error)
            *error = QObject::tr("Corrupt CopyQ export: invalid format count %1.").arg(marker);
        stream->setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    for (qint32 i = 0; i < marker; ++i) {
        QString mime;
        *stream >> mime;
        QByteArray payload;
        *stream >> payload;
        if (stream->status() != QDataStream::Ok) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: cannot read item data.");
            return false;
        }
        if (!payload.isEmpty()) {
            const QByteArray uncompressed = qUncompress(payload);
            if (uncompressed.isEmpty()) {
                if (error)
                    *error = QObject::tr("Corrupt CopyQ export: cannot decompress item data.");
                stream->setStatus(QDataStream::ReadCorruptData);
                return false;
            }
            payload = uncompressed;
        }
        if (mime.startsWith(QLatin1String("FILE:")))
            continue;
        data->insert(mime, payload);
    }
    return stream->status() == QDataStream::Ok;
}

bool readTabBytes(const QByteArray &tabBytes, QVector<QVariantMap> *items, QString *error)
{
    QDataStream tabIn(tabBytes);
    tabIn.setVersion(QDataStream::Qt_4_7);
    qint32 rowCount = 0;
    tabIn >> rowCount;
    if (tabIn.status() != QDataStream::Ok) {
        if (error)
            *error = QObject::tr("Corrupt CopyQ export: cannot read tab contents.");
        return false;
    }
    if (rowCount < 0 || rowCount > kMaxItems) {
        if (error)
            *error = QObject::tr("Corrupt CopyQ export: invalid item count %1.").arg(rowCount);
        return false;
    }
    items->reserve(items->size() + rowCount);
    for (qint32 i = 0; i < rowCount; ++i) {
        QVariantMap data;
        if (!readOneItem(&tabIn, &data, error))
            return false;
        items->append(data);
    }
    return tabIn.status() == QDataStream::Ok;
}

bool readTabs(const QString &path, QVector<Tab> *tabs, QString *error)
{
    const QFileInfo info(path);
    if (info.isSymLink()) {
        if (error)
            *error = QObject::tr("A CopyQ export cannot be imported through a symbolic link.");
        return false;
    }
    if (!info.isFile() || info.size() <= 0 || info.size() > kMaxFileBytes) {
        if (error) {
            *error = !info.exists()
                ? QObject::tr("CopyQ export not found: %1.").arg(path)
                : QObject::tr("The CopyQ file %1 is not a regular file or is too large.").arg(path);
        }
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read %1: %2.").arg(path, file.errorString());
        return false;
    }
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_4_7);
    QByteArray header;
    in >> header;
    if (in.status() != QDataStream::Ok) {
        if (error)
            *error = QObject::tr("%1 is not a CopyQ export (cannot read its header).").arg(path);
        return false;
    }
    if (header.startsWith("CopyQ v5")) {
        if (error)
            *error = QObject::tr("This CopyQ export is encrypted (CopyQ v5, password-protected); "
                                 "export it from CopyQ without a password and import the "
                                 "unencrypted file.");
        return false;
    }
    if (header.startsWith("CopyQ v4")) {
        QVariantMap settings;
        in >> settings;
        if (in.status() != QDataStream::Ok) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: cannot read its settings.");
            return false;
        }
        int total = 0;
        while (!in.atEnd()) {
            QVariantMap tabMap;
            in >> tabMap;
            if (in.status() != QDataStream::Ok) {
                if (error)
                    *error = QObject::tr("Corrupt CopyQ export: cannot read a tab.");
                return false;
            }
            const QString name = tabMap.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                if (error)
                    *error = QObject::tr("Corrupt CopyQ export: a tab has no name.");
                return false;
            }
            if (tabs->size() >= kMaxTabs) {
                if (error)
                    *error = QObject::tr("The CopyQ export holds too many tabs.");
                return false;
            }
            Tab tab;
            tab.name = name.left(256);
            if (!readTabBytes(tabMap.value(QStringLiteral("data")).toByteArray(), &tab.items,
                              error))
                return false;
            total += tab.items.size();
            if (total > kMaxItems) {
                if (error)
                    *error = QObject::tr("The CopyQ export holds too many entries.");
                return false;
            }
            tabs->append(tab);
        }
        return in.status() == QDataStream::Ok;
    }
    if (header.startsWith("CopyQ v3")) {
        QVariantMap settings;
        in >> settings;
        if (in.status() != QDataStream::Ok) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: cannot read its settings.");
            return false;
        }
        const QVariantList tabList = settings.value(QStringLiteral("tabs")).toList();
        if (tabList.size() > kMaxTabs) {
            if (error)
                *error = QObject::tr("The CopyQ export holds too many tabs.");
            return false;
        }
        int total = 0;
        for (const QVariant &entry : tabList) {
            const QVariantMap tabMap = entry.toMap();
            const QString name = tabMap.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                if (error)
                    *error = QObject::tr("Corrupt CopyQ export: a tab has no name.");
                return false;
            }
            Tab tab;
            tab.name = name.left(256);
            if (!readTabBytes(tabMap.value(QStringLiteral("data")).toByteArray(), &tab.items,
                              error))
                return false;
            total += tab.items.size();
            if (total > kMaxItems) {
                if (error)
                    *error = QObject::tr("The CopyQ export holds too many entries.");
                return false;
            }
            tabs->append(tab);
        }
        return in.status() == QDataStream::Ok;
    }
    if (header.startsWith("CopyQ v2") || header.startsWith("CopyQ v1")) {
        QString tabName;
        in >> tabName;
        if (in.status() != QDataStream::Ok || tabName.isEmpty()) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: cannot read its tab.");
            return false;
        }
        // The rest of the stream is the model body: row count + items.
        // Rewind it through a buffer so the row reader sees the versioned
        // stream from the current position.
        Tab tab;
        tab.name = tabName.left(256);
        // Read remaining bytes and parse as tab body.
        const QByteArray rest = file.readAll();
        if (in.status() != QDataStream::Ok) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: cannot read tab contents.");
            return false;
        }
        QDataStream tabIn(rest);
        tabIn.setVersion(QDataStream::Qt_4_7);
        qint32 rowCount = 0;
        tabIn >> rowCount;
        if (tabIn.status() != QDataStream::Ok || rowCount < 0 || rowCount > kMaxItems) {
            if (error)
                *error = QObject::tr("Corrupt CopyQ export: invalid item count.");
            return false;
        }
        for (qint32 i = 0; i < rowCount; ++i) {
            QVariantMap data;
            if (!readOneItem(&tabIn, &data, error))
                return false;
            tab.items.append(data);
        }
        tabs->append(tab);
        return true;
    }
    if (error) {
        const QString head = QString::fromLatin1(header.left(32));
        *error = QObject::tr("%1 is not a supported CopyQ export (header %2; expected "
                             "CopyQ v1..v4 unencrypted, v5 is password-encrypted).")
                     .arg(path, head.isEmpty() ? QObject::tr("unknown") : head);
    }
    return false;
}

QByteArray hashFor(ContentType type, const QByteArray &payload)
{
    return QCryptographicHash::hash(QByteArray(contentTypeTag(type)) + '\0' + payload,
                                    QCryptographicHash::Sha256)
        .toHex();
}

bool toRecord(const QVariantMap &data, ClipboardRecord *record, QString *skipReason)
{
    const auto utf8Text = [](const QVariant &value) {
        return QString::fromUtf8(value.toByteArray());
    };
    QString text;
    if (data.contains(QStringLiteral("text/plain;charset=utf-8")))
        text = utf8Text(data.value(QStringLiteral("text/plain;charset=utf-8")));
    else if (data.contains(QStringLiteral("text/plain")))
        text = utf8Text(data.value(QStringLiteral("text/plain")));
    if (!text.trimmed().isEmpty()) {
        text = text.left(kMaxTextChars);
        record->type = ContentType::Text;
        record->textData = text;
        record->preview = singleLinePreview(text);
        record->sizeBytes = text.toUtf8().size();
        record->hash = hashFor(ContentType::Text, text.toUtf8());
        return true;
    }
    if (data.contains(QStringLiteral("text/html"))) {
        QString html = utf8Text(data.value(QStringLiteral("text/html")));
        if (!html.trimmed().isEmpty()) {
            html = html.left(kMaxTextChars);
            record->type = ContentType::RichText;
            record->textData = html;
            record->preview = singleLinePreview(html);
            record->sizeBytes = html.toUtf8().size();
            record->hash = hashFor(ContentType::RichText, html.toUtf8());
            return true;
        }
    }
    if (data.contains(QStringLiteral("text/uri-list"))) {
        const QString raw = utf8Text(data.value(QStringLiteral("text/uri-list")));
        QStringList paths;
        for (const QString &line : raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
                continue;
            const QUrl url(trimmed);
            const QString local = url.isLocalFile() ? url.toLocalFile() : trimmed;
            if (!local.isEmpty())
                paths.append(local);
            if (paths.size() >= 64)
                break;
        }
        if (!paths.isEmpty()) {
            QJsonArray array;
            for (const QString &p : paths)
                array.append(p);
            const QString json =
                QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
            record->type = ContentType::Files;
            record->textData = json;
            QString names;
            for (const QString &p : paths) {
                if (!names.isEmpty())
                    names += QStringLiteral(", ");
                names += QFileInfo(p).fileName();
            }
            record->preview = QObject::tr("%1 %2: %3")
                                  .arg(paths.size())
                                  .arg(paths.size() == 1 ? QObject::tr("file")
                                                         : QObject::tr("files"))
                                  .arg(singleLinePreview(names));
            record->sizeBytes = json.toUtf8().size();
            record->hash = hashFor(ContentType::Files, json.toUtf8());
            return true;
        }
        if (!raw.trimmed().isEmpty()) {
            const QString fallback = raw.left(kMaxTextChars);
            record->type = ContentType::Text;
            record->textData = fallback;
            record->preview = singleLinePreview(fallback);
            record->sizeBytes = fallback.toUtf8().size();
            record->hash = hashFor(ContentType::Text, fallback.toUtf8());
            return true;
        }
    }
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("image/"))) {
            const QByteArray blob = it.value().toByteArray();
            if (blob.isEmpty())
                continue;
            if (blob.size() > kMaxBlobBytes) {
                if (skipReason)
                    *skipReason = QObject::tr("image exceeds the size limit");
                return false;
            }
            record->type = ContentType::Image;
            record->blobData = blob;
            record->hasBlob = true;
            record->preview = QStringLiteral("image");
            record->sizeBytes = blob.size();
            record->hash = hashFor(ContentType::Image, blob);
            return true;
        }
    }
    if (skipReason)
        *skipReason = QObject::tr("no usable text, file, or image payload");
    return false;
}

} // namespace CopyQImport

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

bool ExportImportManager::exportToFile(const ExportRequest &request, QString *error,
                                       std::atomic<bool> *cancel, IoProgress progress,
                                       int *skippedSensitive)
{
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (isCanceled()) {
        if (error)
            *error = tr("Export canceled before it started.");
        return false;
    }
    const QVector<BookmarkGroup> allGroups = m_bookmarks->groups();
    QSet<qint64> exportedGroupIds;
    for (const BookmarkGroup &group : allGroups)
        exportedGroupIds.insert(group.id);

    // --- gather entries, paged + cancelable ----------------------------------
    // Everything/PinnedOnly stream bounded keyset pages (never fetchAllFull);
    // GroupSubtree batches its id list through the same IN-query helper.
    FilterSpec filter;
    QVector<ClipboardRecord> entries;
    int estimatedTotal = 0;
    if (request.scope == Scope::Everything || request.scope == Scope::PinnedOnly) {
        if (request.scope == Scope::PinnedOnly)
            filter.pinnedOnly = true;
        estimatedTotal = int(request.scope == Scope::PinnedOnly
                                 ? m_storage->stats().pinnedCount
                                 : m_storage->stats().entryCount);
        PageCursor cursor;
        while (true) {
            if (isCanceled()) {
                if (error)
                    *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                                 .arg(entries.size())
                                 .arg(qMax(estimatedTotal, entries.size()));
                return false;
            }
            bool hasMore = false;
            const QVector<ClipboardRecord> page =
                m_storage->fetchPage(filter, cursor, kImageExportPageSize, &hasMore);
            if (page.isEmpty())
                break;
            QList<qint64> ids;
            ids.reserve(page.size());
            for (const ClipboardRecord &summary : page)
                ids.append(summary.id);
            QVector<ClipboardRecord> payloads;
            QString batchError;
            if (!fetchPayloadBatch(ids, &payloads, &batchError)) {
                if (error)
                    *error = batchError.isEmpty() ? tr("Database read failed.") : batchError;
                return false;
            }
            entries.append(payloads);
            if (progress)
                progress(entries.size(), qMax(estimatedTotal, entries.size()));
            if (!hasMore)
                break;
            // Same cursor discipline as fetchAllFull: every key the active
            // sort compares must travel, or paging silently truncates.
            cursor = PageCursor{true, page.last().timestamp, page.last().id,
                                page.last().useCount};
        }
    } else {
        exportedGroupIds =
            descendantGroupIds(allGroups, request.groupId);
        const QList<qint64> listedIds = m_bookmarks->entryIdsForGroups(exportedGroupIds);
        estimatedTotal = listedIds.size();
        for (int offset = 0; offset < listedIds.size();) {
            if (isCanceled()) {
                if (error)
                    *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                                 .arg(entries.size())
                                 .arg(qMax(estimatedTotal, entries.size()));
                return false;
            }
            const int chunk = qMin(kImageExportPageSize, listedIds.size() - offset);
            QVector<ClipboardRecord> payloads;
            QString batchError;
            if (!fetchPayloadBatch(listedIds.mid(offset, chunk), &payloads, &batchError)) {
                if (error)
                    *error = batchError.isEmpty() ? tr("Database read failed.") : batchError;
                return false;
            }
            offset += chunk;
            entries.append(payloads);
            if (progress)
                progress(entries.size(), qMax(estimatedTotal, entries.size()));
        }
    }
    // Sensitive policy: user exports leave secrets out unless explicitly
    // opted in (mirrors the image-export gate). Backups always opt in.
    // Filtering here keeps memberships consistent too (built from `entries`).
    int skipped = 0;
    if (!request.includeSensitive) {
        QVector<ClipboardRecord> kept;
        kept.reserve(entries.size());
        for (const ClipboardRecord &record : entries) {
            if (record.sensitive)
                ++skipped;
            else
                kept.append(record);
        }
        entries = kept;
    }
    if (skippedSensitive)
        *skippedSensitive = skipped;
    // Serialize phase counts on top of the gather phase so progress stays
    // monotonic even when the history changed mid-run.
    const int serializeBase = qMax(estimatedTotal, entries.size());
    const int grandTotal = serializeBase + entries.size();
    int serialized = 0;
    const auto reportSerialize = [&] {
        if (progress)
            progress(serializeBase + serialized, grandTotal);
    };

    QList<qint64> tagEntryIds;
    tagEntryIds.reserve(entries.size());
    for (const ClipboardRecord &record : entries)
        tagEntryIds.append(record.id);
    QHash<qint64, QStringList> tagsByEntry;
    if (!fetchTagsBatch(tagEntryIds, &tagsByEntry, error, cancel))
        return false;

    // --- reading formats stop here (entries only, no re-import) ---------------
    if (request.format == ExportFormat::Csv)
        return writeCsvExport(request, entries, tagsByEntry, error, cancel, progress,
                              serializeBase, grandTotal);
    if (request.format == ExportFormat::Markdown)
        return writeMarkdownExport(request, entries, tagsByEntry, error, cancel, progress,
                                   serializeBase, grandTotal);
    if (request.format == ExportFormat::Html)
        return writeHtmlExport(request, entries, tagsByEntry, error, cancel, progress,
                               serializeBase, grandTotal);

    // --- memberships --------------------------------------------------------
    QJsonArray memberships;
    QList<qint64> membershipEntryIds;
    membershipEntryIds.reserve(entries.size());
    for (const ClipboardRecord &record : entries)
        membershipEntryIds.append(record.id);
    QHash<qint64, QList<qint64>> groupsByEntry;
    if (!m_bookmarks->groupIdsForEntries(membershipEntryIds, &groupsByEntry)) {
        if (error)
            *error = tr("Database read failed while loading group memberships.");
        return false;
    }
    if (request.scope != Scope::PinnedOnly) {
        for (const ClipboardRecord &record : entries) {
            if (isCanceled()) {
                if (error)
                    *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                                 .arg(serializeBase + serialized)
                                 .arg(grandTotal);
                return false;
            }
            for (const qint64 gid : groupsByEntry.value(record.id)) {
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
            if (isCanceled()) {
                if (error)
                    *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                                 .arg(serializeBase + serialized)
                                 .arg(grandTotal);
                return false;
            }
            for (const qint64 gid : groupsByEntry.value(record.id)) {
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
        if (isCanceled()) {
            if (error)
                *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                             .arg(serializeBase + serialized)
                             .arg(grandTotal);
            return false;
        }
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
        const QStringList tags = tagsByEntry.value(record.id);
        if (!tags.isEmpty()) {
            QJsonArray tagArray;
            for (const QString &tag : tags)
                tagArray.append(tag);
            object.insert(QStringLiteral("tags"), tagArray);
        }
        entryArray.append(object);
        if (++serialized % 64 == 0)
            reportSerialize();
    }
    reportSerialize();

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
    file.close();
    // Full history (incl. secrets) as plaintext: owner-only.
    QFile::setPermissions(request.path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool ExportImportManager::fetchPayloadBatch(const QList<qint64> &ids, QVector<ClipboardRecord> *out,
                                            QString *error)
{
    if (ids.isEmpty())
        return true;
    QStringList placeholders;
    placeholders.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i)
        placeholders << QStringLiteral("?");
    QSqlQuery query(m_storage->database());
    query.prepare(QStringLiteral(
        "SELECT id, timestamp_ms, content_type, content_hash, text_data, blob_data, preview,"
        " size_bytes, pinned, sensitive, use_count, source_app, source_window, ocr_text"
        " FROM entries WHERE id IN (%1)").arg(placeholders.join(QLatin1Char(','))));
    for (int i = 0; i < ids.size(); ++i)
        query.bindValue(i, ids.at(i));
    if (!query.exec()) {
        if (error)
            *error = tr("Database read failed: %1").arg(query.lastError().text());
        return false;
    }
    QHash<qint64, ClipboardRecord> byId;
    byId.reserve(ids.size());
    while (query.next()) {
        ClipboardRecord record;
        record.id = query.value(0).toLongLong();
        record.timestamp = query.value(1).toLongLong();
        record.type = static_cast<ContentType>(query.value(2).toInt());
        record.hash = query.value(3).toByteArray();
        record.textData = query.value(4).toString();
        record.blobData = query.value(5).toByteArray();
        record.hasBlob = !record.blobData.isEmpty();
        record.preview = query.value(6).toString();
        record.sizeBytes = query.value(7).toLongLong();
        record.pinned = query.value(8).toInt() != 0;
        record.sensitive = query.value(9).toInt() != 0;
        record.useCount = query.value(10).toInt();
        record.sourceApp = query.value(11).toString();
        record.sourceWindow = query.value(12).toString();
        record.ocrText = query.value(13).toString();
        byId.insert(record.id, record);
    }
    out->reserve(out->size() + ids.size());
    for (const qint64 id : ids) {
        const auto it = byId.constFind(id);
        if (it != byId.constEnd())
            out->append(*it);
    }
    return true;
}

bool ExportImportManager::fetchTagsBatch(const QList<qint64> &ids,
                                         QHash<qint64, QStringList> *tagsByEntry,
                                         QString *error, std::atomic<bool> *cancel) const
{
    if (!tagsByEntry)
        return false;
    tagsByEntry->clear();
    static constexpr int kTagQueryBatchSize = 500;
    for (int offset = 0; offset < ids.size(); offset += kTagQueryBatchSize) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            if (error)
                *error = tr("Export canceled while reading tags — no file was written.");
            return false;
        }
        const int count = qMin(kTagQueryBatchSize, ids.size() - offset);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i)
            placeholders.append(QStringLiteral("?"));
        QSqlQuery query(m_storage->database());
        query.prepare(QStringLiteral(
            "SELECT et.entry_id, t.name FROM entry_tags et"
            " JOIN tags t ON t.id = et.tag_id"
            " WHERE et.entry_id IN (%1) ORDER BY et.entry_id, t.name COLLATE NOCASE")
                          .arg(placeholders.join(QLatin1Char(','))));
        for (int i = 0; i < count; ++i)
            query.bindValue(i, ids.at(offset + i));
        if (!query.exec()) {
            if (error)
                *error = tr("Database read failed: %1").arg(query.lastError().text());
            return false;
        }
        while (query.next())
            (*tagsByEntry)[query.value(0).toLongLong()].append(query.value(1).toString());
    }
    return true;
}

ExportImportManager::ImageExportResult
ExportImportManager::exportImages(const ImageExportRequest &request,
                                  std::atomic<bool> *cancel,
                                  ImageExportProgress progress,
                                  ImageEncoder encode)
{
    ImageExportResult result;
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    const auto reportProgress = [&] {
        if (progress)
            progress(result.exported,
                     result.skippedNoBlob + result.skippedNonImage + result.skippedSensitive,
                     result.bytesWritten);
    };
    if (isCanceled()) {
        result.canceled = true;
        result.error = tr("Image export canceled before it started.");
        return result;
    }
    if (request.fileFormat == ImageExportRequest::ImageFileFormat::Jpeg && !encode) {
        result.error = tr("JPEG conversion is unavailable: no image encoder was provided.");
        return result;
    }
    if (request.dir.trimmed().isEmpty()) {
        result.error = tr("No target folder selected.");
        return result;
    }
    QDir dir(request.dir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        result.error = tr("Cannot create the folder %1.").arg(request.dir);
        return result;
    }
    const QString absDir = dir.absolutePath();

    // --- resolve candidates ---------------------------------------------------
    // Selection/group scopes enumerate bounded id lists; filter scopes stream
    // keyset pages so memory stays flat no matter how large the history is.
    QList<qint64> listedIds;
    if (request.scope == ImageExportRequest::Scope::Selection) {
        QSet<qint64> seen;
        listedIds.reserve(request.entryIds.size());
        for (const qint64 id : request.entryIds) {
            if (id != 0 && !seen.contains(id)) {
                seen.insert(id);
                listedIds.append(id);
            }
        }
    } else if (request.scope == ImageExportRequest::Scope::GroupSubtree) {
        const QSet<qint64> groupIds = descendantGroupIds(m_bookmarks->groups(), request.groupId);
        listedIds = m_bookmarks->entryIdsForGroups(groupIds);
    }
    FilterSpec filter;
    if (request.scope == ImageExportRequest::Scope::CurrentFilter)
        filter = request.filter;
    else if (request.scope == ImageExportRequest::Scope::PinnedOnly)
        filter.pinnedOnly = true;
    const bool streamed = request.scope == ImageExportRequest::Scope::Everything
        || request.scope == ImageExportRequest::Scope::CurrentFilter
        || request.scope == ImageExportRequest::Scope::PinnedOnly;

    QJsonArray manifestFiles;
    QString batchError;
    bool readFailed = false;

    // Writes one image record; false only on cancel or an I/O failure.
    const auto writeRecord = [&](const ClipboardRecord &record, const QStringList &tags) {
        if (isCanceled())
            return false;
        if (record.type != ContentType::Image) {
            ++result.skippedNonImage;
            return true;
        }
        if (record.sensitive && !request.includeSensitive) {
            ++result.skippedSensitive;
            return true;
        }
        if (record.blobData.isEmpty()) {
            ++result.skippedNoBlob;
            return true;
        }
        // Pixel encoding: verbatim PNG by default, caller-provided conversion
        // (e.g. JPEG via QImage in the app layer) when an encoder is set.
        QByteArray payload = record.blobData;
        QString extension = QStringLiteral("png");
        if (encode) {
            QString encodeError;
            QString encodedExtension;
            payload = encode(record.blobData, record.id, &encodedExtension, &encodeError);
            if (payload.isEmpty()) {
                result.error = encodeError.isEmpty()
                    ? tr("Cannot convert the image of entry %1.").arg(record.id)
                    : encodeError;
                return false;
            }
            if (!encodedExtension.trimmed().isEmpty())
                extension = encodedExtension.trimmed().toLower();
        }
        const QString stem = imageFileBaseName(record);
        QString name = stem + QLatin1Char('.') + extension;
        QString path;
        QFile file;
        bool opened = false;
        for (int attempt = 0; attempt < kImageExportNameAttempts; ++attempt) {
            if (attempt > 0)
                name = QStringLiteral("%1-%2.%3").arg(stem).arg(attempt + 1).arg(extension);
            path = absDir + QLatin1Char('/') + name;
            file.setFileName(path);
            // NewOnly: an existing file — ours or the user's — is never
            // overwritten; the next suffix is tried instead.
            if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                opened = true;
            if (opened || file.error() != QFile::FileError::OpenError)
                break;
        }
        if (!opened) {
            result.error = tr("Cannot write %1: %2").arg(path, file.errorString());
            return false;
        }
        if (file.write(payload) != payload.size()) {
            const QString writeError = file.errorString();
            file.close();
            file.remove(); // no truncated image is left behind
            result.error = tr("Write to %1 failed: %2").arg(path, writeError);
            return false;
        }
        file.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        ++result.exported;
        result.bytesWritten += payload.size();
        result.files.append(path);

        QJsonObject entry;
        entry.insert(QStringLiteral("file"), name);
        entry.insert(QStringLiteral("id"), double(record.id));
        entry.insert(QStringLiteral("timestamp"),
                     QDateTime::fromMSecsSinceEpoch(record.timestamp).toString(Qt::ISODateWithMs));
        entry.insert(QStringLiteral("sourceApp"), record.sourceApp);
        entry.insert(QStringLiteral("sourceWindow"), record.sourceWindow);
        entry.insert(QStringLiteral("pinned"), record.pinned);
        entry.insert(QStringLiteral("sensitive"), record.sensitive);
        if (!tags.isEmpty()) {
            QJsonArray tagArray;
            for (const QString &tag : tags)
                tagArray.append(tag);
            entry.insert(QStringLiteral("tags"), tagArray);
        }
        if (!record.ocrText.isEmpty())
            entry.insert(QStringLiteral("ocrText"), record.ocrText);
        // Payload text stays out of the manifest unless explicitly requested.
        if (request.includeText && !record.textData.isEmpty())
            entry.insert(QStringLiteral("text"), record.textData);
        manifestFiles.append(entry);
        return true;
    };
    const auto fetchImageTags = [&](const QVector<ClipboardRecord> &records,
                                    QHash<qint64, QStringList> *tagsByEntry) {
        QList<qint64> ids;
        ids.reserve(records.size());
        for (const ClipboardRecord &record : records) {
            if (record.type == ContentType::Image
                && (request.includeSensitive || !record.sensitive)
                && !record.blobData.isEmpty())
                ids.append(record.id);
        }
        return fetchTagsBatch(ids, tagsByEntry, &batchError, cancel);
    };

    if (streamed) {
        PageCursor cursor;
        while (true) {
            if (isCanceled())
                break;
            bool hasMore = false;
            const QVector<ClipboardRecord> page =
                m_storage->fetchPage(filter, cursor, kImageExportPageSize, &hasMore);
            if (page.isEmpty())
                break;
            QVector<qint64> ids;
            ids.reserve(page.size());
            for (const ClipboardRecord &summary : page)
                ids.append(summary.id);
            QVector<ClipboardRecord> payloads;
            if (!fetchPayloadBatch(ids, &payloads, &batchError)) {
                readFailed = true;
                break;
            }
            QHash<qint64, QStringList> tagsByEntry;
            if (!fetchImageTags(payloads, &tagsByEntry)) {
                readFailed = true;
                break;
            }
            bool stopped = false;
            for (const ClipboardRecord &record : payloads) {
                if (!writeRecord(record, tagsByEntry.value(record.id))) {
                    stopped = true;
                    break;
                }
            }
            reportProgress();
            if (stopped)
                break;
            if (!hasMore)
                break;
            // Same cursor discipline as fetchAllFull: every key the active
            // sort compares must travel, or paging silently truncates.
            cursor = PageCursor{true, page.last().timestamp, page.last().id,
                                page.last().useCount};
        }
    } else {
        for (int offset = 0; offset < listedIds.size();) {
            if (isCanceled())
                break;
            const int chunk = qMin(kImageExportPageSize, listedIds.size() - offset);
            QVector<qint64> ids;
            ids.reserve(chunk);
            for (int i = 0; i < chunk; ++i)
                ids.append(listedIds.at(offset + i));
            offset += chunk;
            QVector<ClipboardRecord> payloads;
            if (!fetchPayloadBatch(ids, &payloads, &batchError)) {
                readFailed = true;
                break;
            }
            QHash<qint64, QStringList> tagsByEntry;
            if (!fetchImageTags(payloads, &tagsByEntry)) {
                readFailed = true;
                break;
            }
            bool stopped = false;
            for (const ClipboardRecord &record : payloads) {
                if (!writeRecord(record, tagsByEntry.value(record.id))) {
                    stopped = true;
                    break;
                }
            }
            reportProgress();
            if (stopped)
                break;
        }
    }
    reportProgress();

    if (isCanceled()) {
        result.canceled = true;
        result.error = tr("Image export canceled after %n file(s) were written. "
                          "No manifest was written.", nullptr, result.exported);
        return result;
    }
    if (readFailed) {
        result.error = batchError.isEmpty() ? tr("Database read failed.") : batchError;
        return result;
    }
    if (!result.error.isEmpty()) // writeRecord reported an I/O failure above
        return result;

    // --- manifest (only on a completed run) ----------------------------------
    QJsonObject counts;
    counts.insert(QStringLiteral("exported"), result.exported);
    counts.insert(QStringLiteral("skippedNoBlob"), result.skippedNoBlob);
    counts.insert(QStringLiteral("skippedNonImage"), result.skippedNonImage);
    counts.insert(QStringLiteral("skippedSensitive"), result.skippedSensitive);
    counts.insert(QStringLiteral("bytesWritten"), double(result.bytesWritten));
    QJsonObject root;
    root.insert(QStringLiteral("format"), imageExportFormatTag());
    root.insert(QStringLiteral("version"), imageExportFormatVersion());
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("scope"), imageScopeName(request.scope));
    root.insert(QStringLiteral("fileFormat"),
                ImageExportRequest::imageFileFormatName(request.fileFormat));
    if (request.fileFormat == ImageExportRequest::ImageFileFormat::Jpeg)
        root.insert(QStringLiteral("jpegQuality"), qBound(1, request.jpegQuality, 100));
    root.insert(QStringLiteral("includeSensitive"), request.includeSensitive);
    root.insert(QStringLiteral("includeText"), request.includeText);
    root.insert(QStringLiteral("counts"), counts);
    root.insert(QStringLiteral("files"), manifestFiles);

    QString manifestPath;
    QFile manifest;
    bool manifestOpened = false;
    for (int attempt = 0; attempt < kImageExportNameAttempts; ++attempt) {
        manifestPath = absDir
            + (attempt == 0 ? QStringLiteral("/manifest.json")
                            : QStringLiteral("/manifest-%1.json").arg(attempt + 1));
        manifest.setFileName(manifestPath);
        if (manifest.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            manifestOpened = true;
            break;
        }
        if (manifest.error() != QFile::FileError::OpenError)
            break;
    }
    if (!manifestOpened) {
        result.error = tr("Images were written, but the manifest could not be created "
                          "in %1: %2. The image files are on disk; export them again "
                          "into an empty folder to get a manifest.")
                           .arg(absDir, manifest.errorString());
        return result;
    }
    const QByteArray manifestPayload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (manifest.write(manifestPayload) != manifestPayload.size()) {
        const QString writeError = manifest.errorString();
        manifest.close();
        manifest.remove();
        result.error = tr("Images were written, but writing the manifest failed: %1. "
                          "The image files are on disk; export them again into an empty "
                          "folder to get a manifest.").arg(writeError);
        return result;
    }
    result.manifestPath = manifestPath;
    result.ok = true;
    return result;
}

ExportImportManager::ImportResult
ExportImportManager::importFromFile(const QString &path, ImportMode mode,
                                    std::atomic<bool> *cancel, IoProgress progress)
{
    ImportResult result;
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    QFile file(path);
    // Bound the deserialization: a crafted multi-GB file must not OOM the GUI.
    constexpr qint64 kMaxImportBytes = 100 * 1024 * 1024;
    constexpr int kMaxImportEntries = 500000;
    constexpr int kMaxBlobBytes = 10 * 1024 * 1024;
    if (file.size() > kMaxImportBytes) {
        result.error = tr("%1 is too large to import (%2 bytes)").arg(path).arg(file.size());
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = tr("Cannot read %1: %2").arg(path, file.errorString());
        return result;
    }
    const QByteArray raw = file.read(kMaxImportBytes + 1);
    if (raw.size() > kMaxImportBytes) {
        result.error = tr("%1 is too large to import").arg(path);
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
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
    if (isCanceled()) {
        result.error = tr("Import canceled before it started — nothing was imported.");
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
    const QHash<qint64, QString> importedPaths = groupPaths(importedGroups);
    QVector<BookmarkGroup> existing = m_bookmarks->groups();
    QHash<qint64, QString> existingPaths = groupPaths(existing);
    QHash<QString, qint64> existingByPath;
    existingByPath.reserve(existing.size());
    for (const BookmarkGroup &candidate : existing) {
        const auto path = existingPaths.constFind(candidate.id);
        // Preserve the old first-match behavior when duplicate paths exist.
        if (path != existingPaths.cend() && !existingByPath.contains(path.value()))
            existingByPath.insert(path.value(), candidate.id);
    }
    const auto importGroup = [&](const BookmarkGroup &imported, qint64 parentId) {
        const QString path = importedPaths.value(imported.id);
        const auto match = existingByPath.constFind(path);
        if (match != existingByPath.cend()) {
            groupIdMap.insert(imported.id, match.value());
            return;
        }
        const qint64 created =
            m_bookmarks->createGroup(imported.name, parentId, imported.color, imported.icon);
        if (created != 0) {
            groupIdMap.insert(imported.id, created);
            ++result.groupsImported;
            const QString parentPath = existingPaths.value(parentId);
            const QString createdPath = parentPath.isEmpty()
                ? imported.name
                : parentPath + QLatin1Char('/') + imported.name;
            existingPaths.insert(created, createdPath);
            if (!existingByPath.contains(createdPath))
                existingByPath.insert(createdPath, created);
        }
    };

    // The JSON array is name-ordered, so a child may appear before its
    // parent. Walk a stable, index-ordered ready queue to preserve the old
    // pass ordering without rescanning every pending group for each level.
    QSet<qint64> pendingGroupIds;
    QHash<qint64, int> firstIndexById;
    firstIndexById.reserve(importedGroups.size());
    for (int i = 0; i < importedGroups.size(); ++i) {
        const BookmarkGroup &group = importedGroups.at(i);
        pendingGroupIds.insert(group.id);
        if (!firstIndexById.contains(group.id))
            firstIndexById.insert(group.id, i);
    }
    QHash<qint64, QVector<int>> childrenByParent;
    childrenByParent.reserve(importedGroups.size());
    std::priority_queue<int, std::vector<int>, std::greater<int>> readyGroups;
    for (int i = 0; i < importedGroups.size(); ++i) {
        const BookmarkGroup &group = importedGroups.at(i);
        if (firstIndexById.value(group.id) != i)
            continue; // duplicate IDs were skipped after the first record
        childrenByParent[group.parentId].append(i);
        if (group.parentId == 0 || !pendingGroupIds.contains(group.parentId))
            readyGroups.push(i); // roots and references to missing parents
    }

    QVector<int> nextPass;
    nextPass.reserve(importedGroups.size());
    while (!readyGroups.empty()) {
        nextPass.clear();
        while (!readyGroups.empty()) {
            const int index = readyGroups.top();
            readyGroups.pop();
            const BookmarkGroup &imported = importedGroups.at(index);
            if (!pendingGroupIds.contains(imported.id))
                continue;
            importGroup(imported, groupIdMap.value(imported.parentId, 0));
            pendingGroupIds.remove(imported.id);

            const auto children = childrenByParent.constFind(imported.id);
            if (children == childrenByParent.cend())
                continue;
            for (const int childIndex : children.value()) {
                const BookmarkGroup &child = importedGroups.at(childIndex);
                if (!pendingGroupIds.contains(child.id))
                    continue;
                if (childIndex > index)
                    readyGroups.push(childIndex); // later in this scan pass
                else
                    nextPass.append(childIndex); // already passed this index
            }
        }
        for (const int index : nextPass)
            readyGroups.push(index);
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
    if (entryArray.size() > kMaxImportEntries) {
        if (bulk)
            m_storage->endBulk(false);
        result.error = tr("%1 holds too many entries (%2)").arg(path).arg(entryArray.size());
        return result;
    }
    QHash<QByteArray, qint64> entryIdByHash; // imported hash -> local id
    const int entryTotal = entryArray.size();
    int entryDone = 0;
    const auto abortImport = [&](const QString &error) {
        if (bulk)
            m_storage->endBulk(false); // roll back: a canceled import imports nothing
        result.error = error;
        return result;
    };
    // Statements hoisted from the loop: one prepare, N executions (the loop
    // runs once per imported entry, up to the whole history).
    QSqlQuery findDuplicate(db);
    findDuplicate.prepare(QStringLiteral("SELECT id FROM entries WHERE content_hash = :h LIMIT 1"));
    QSqlQuery mergeUpdate(db);
    mergeUpdate.prepare(QStringLiteral(
        "UPDATE entries SET timestamp_ms = :ts, pinned = MAX(pinned, :p),"
        " use_count = MAX(use_count, :uc), sensitive = MAX(sensitive, :s)"
        " WHERE id = :id"));
    QSqlQuery fixUseCount(db);
    fixUseCount.prepare(QStringLiteral("UPDATE entries SET use_count = :uc WHERE id = :id"));
    const auto existingIdFor = [&findDuplicate](const QByteArray &hash) -> std::optional<qint64> {
        findDuplicate.bindValue(QStringLiteral(":h"), QString::fromLatin1(hash));
        if (findDuplicate.exec() && findDuplicate.next())
            return findDuplicate.value(0).toLongLong();
        return std::nullopt;
    };
    for (const auto &value : entryArray) {
        if (isCanceled())
            return abortImport(tr("Import canceled after %1 of %2 entries — nothing was imported.")
                                   .arg(entryDone)
                                   .arg(entryTotal));
        ++entryDone;
        if (entryDone % 64 == 0 && progress)
            progress(entryDone, entryTotal);
        const QJsonObject object = value.toObject();
        ClipboardRecord record;
        record.hash = object.value(QStringLiteral("hash")).toString().toLatin1();
        // Hash is the dedup key (bound, never interpolated): accept the
        // historical non-hex test vectors too, but bound length so a crafted
        // file cannot bloat memory. Empty still skips.
        if (record.hash.isEmpty() || record.hash.size() > 256)
            continue;
        record.timestamp = qint64(object.value(QStringLiteral("timestamp")).toDouble());
        if (record.timestamp <= 0)
            record.timestamp = 1;
        record.type = typeFromTag(object.value(QStringLiteral("type")).toString());
        record.textData = object.value(QStringLiteral("text")).toString().left(1024 * 1024);
        const QString blobText = object.value(QStringLiteral("blob")).toString();
        if (blobText.size() > (kMaxBlobBytes / 3) * 4 + 4)
            continue; // per-entry blob cap before base64 decode
        record.blobData = QByteArray::fromBase64(blobText.toLatin1());
        if (record.blobData.size() > kMaxBlobBytes)
            continue;
        record.hasBlob = !record.blobData.isEmpty();
        record.preview = object.value(QStringLiteral("preview")).toString().left(4096);
        record.sizeBytes = qint64(object.value(QStringLiteral("sizeBytes")).toDouble());
        record.pinned = object.value(QStringLiteral("pinned")).toBool(false);
        record.sensitive = object.value(QStringLiteral("sensitive")).toBool(false);
        record.useCount = qBound(0, object.value(QStringLiteral("useCount")).toInt(), 1000000000);
        record.sourceApp = object.value(QStringLiteral("sourceApp")).toString().left(256);
        record.sourceWindow = object.value(QStringLiteral("sourceWindow")).toString().left(512);
        record.ocrText = object.value(QStringLiteral("ocrText")).toString().left(1024 * 1024);
        QStringList tagNames;
        const QJsonArray tagArray = object.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &tagValue : tagArray) {
            if (tagNames.size() >= 20)
                break;
            const QString tag = tagValue.toString().trimmed().left(64);
            if (!tag.isEmpty())
                tagNames.append(tag);
        }
        if (record.hash.isEmpty())
            continue;

        const auto existingId = existingIdFor(record.hash);
        if (existingId.has_value()) {
            entryIdByHash.insert(record.hash, *existingId);
            if (mode == ImportMode::SkipDuplicates) {
                ++result.entriesSkipped;
                continue;
            }
            if (mode == ImportMode::Merge) {
                ClipboardRecord existing;
                if (m_storage->fetchFull(*existingId, &existing)) {
                    mergeUpdate.bindValue(
                        QStringLiteral(":ts"),
                        qMax(existing.timestamp, record.timestamp));
                    mergeUpdate.bindValue(QStringLiteral(":p"), record.pinned ? 1 : 0);
                    mergeUpdate.bindValue(QStringLiteral(":uc"), record.useCount);
                    mergeUpdate.bindValue(QStringLiteral(":s"), record.sensitive ? 1 : 0);
                    mergeUpdate.bindValue(QStringLiteral(":id"), *existingId);
                    mergeUpdate.exec();
                    if (!record.ocrText.isEmpty() && existing.ocrText.isEmpty())
                        m_storage->setOcrText(*existingId, record.ocrText);
                }
                applyImportedTags(*existingId, tagNames);
                ++result.entriesMerged;
                continue;
            }
            // Overwrite already wiped local history; a hash hit can only be a
            // duplicate inside this file, so keep its first occurrence.
            ++result.entriesSkipped;
            continue;
        }

        record.id = 0;
        const qint64 insertedId = m_storage->insertOrUpdate(record);
        if (insertedId != 0) {
            entryIdByHash.insert(record.hash, insertedId);
            ++result.entriesImported;
            // insertOrUpdate resets use_count; restore imported value.
            fixUseCount.bindValue(QStringLiteral(":uc"), record.useCount);
            fixUseCount.bindValue(QStringLiteral(":id"), insertedId);
            fixUseCount.exec();
            if (!record.ocrText.isEmpty())
                m_storage->setOcrText(insertedId, record.ocrText);
            applyImportedTags(insertedId, tagNames);
        }
    }
    if (progress)
        progress(entryDone, entryTotal);

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
        if (importedSnippetNames.size() >= 1000)
            break; // snippet library cap
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed().left(128);
        const QString plainTemplate =
            object.value(QStringLiteral("template")).toString().left(64 * 1024);
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
        const QString name = object.value(QStringLiteral("name")).toString().trimmed().left(128);
        if (name.isEmpty())
            continue;
        FilterSpec filter =
            FilterSpec::fromJson(object.value(QStringLiteral("filter")).toObject());
        // A planted /regex/ would otherwise arm ReDoS on every future fetch.
        if (!filter.regexText.isEmpty()) {
            if (filter.regexText.size() > 200)
                continue;
            const QRegularExpression re(filter.regexText);
            if (!re.isValid())
                continue;
        }
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
                                         const QVector<ClipboardRecord> &entries,
                                         const QHash<qint64, QStringList> &tagsByEntry,
                                         QString *error,
                                         std::atomic<bool> *cancel, IoProgress progress,
                                         int progressBase, int progressTotal) const
{
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    QString out;
    out.reserve(entries.size() * 160);
    out += QStringLiteral(
        "timestamp,type,source_app,source_window,pinned,sensitive,use_count,tags,text\n");
    int done = 0;
    for (const ClipboardRecord &record : entries) {
        if (isCanceled()) {
            if (error)
                *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                             .arg(progressBase + done)
                             .arg(progressTotal);
            return false;
        }
        const QString text = record.textData.isEmpty() ? record.preview : record.textData;
        const QString tags = tagsByEntry.value(record.id).join(QStringLiteral("; "));
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
        if (++done % 64 == 0 && progress)
            progress(progressBase + done, progressTotal);
    }
    if (progress)
        progress(progressBase + done, progressTotal);
    return writeTextFile(request.path, out, error);
}

bool ExportImportManager::writeMarkdownExport(const ExportRequest &request,
                                              const QVector<ClipboardRecord> &entries,
                                              const QHash<qint64, QStringList> &tagsByEntry,
                                              QString *error, std::atomic<bool> *cancel,
                                              IoProgress progress, int progressBase,
                                              int progressTotal) const
{
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    QString out;
    out += QStringLiteral("# Egoboard history\n\n");
    out += tr("Exported %1 — %n entry/entries.", nullptr, entries.size())
               .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    out += QStringLiteral("\n");
    int done = 0;
    for (const ClipboardRecord &record : entries) {
        if (isCanceled()) {
            if (error)
                *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                             .arg(progressBase + done)
                             .arg(progressTotal);
            return false;
        }
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
        const QStringList tags = tagsByEntry.value(record.id);
        if (!tags.isEmpty())
            out += tr("Tags: %1\n").arg(tags.join(QStringLiteral(", ")));
        if (record.sourceWindow.isEmpty() == false)
            out += tr("Window: %1\n").arg(record.sourceWindow);
        const QString text = record.textData.isEmpty() ? record.preview : record.textData;
        const QString fence = markdownFence(text);
        out += QStringLiteral("\n") + fence + QStringLiteral("\n") + text + QStringLiteral("\n")
               + fence + QStringLiteral("\n");
        if (++done % 64 == 0 && progress)
            progress(progressBase + done, progressTotal);
    }
    if (progress)
        progress(progressBase + done, progressTotal);
    return writeTextFile(request.path, out, error);
}

bool ExportImportManager::writeHtmlExport(const ExportRequest &request,
                                          const QVector<ClipboardRecord> &entries,
                                          const QHash<qint64, QStringList> &tagsByEntry,
                                          QString *error,
                                          std::atomic<bool> *cancel, IoProgress progress,
                                          int progressBase, int progressTotal) const
{
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
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
    int done = 0;
    for (const ClipboardRecord &record : entries) {
        if (isCanceled()) {
            if (error)
                *error = tr("Export canceled after %1 of %2 entries — no file was written.")
                             .arg(progressBase + done)
                             .arg(progressTotal);
            return false;
        }
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
               + htmlEscaped(tagsByEntry.value(record.id).join(QStringLiteral(", ")))
               + QStringLiteral("</td><td class=\"text\">") + htmlEscaped(text)
               + QStringLiteral("</td></tr>\n");
        if (++done % 64 == 0 && progress)
            progress(progressBase + done, progressTotal);
    }
    if (progress)
        progress(progressBase + done, progressTotal);
    out += QStringLiteral("</tbody></table>\n</body></html>\n");
    return writeTextFile(request.path, out, error);
}

QString ExportImportManager::defaultKlipperPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/klipper/history3.sqlite");
}

ExportImportManager::ImportResult
ExportImportManager::importKlipperHistory(const QString &databasePath, std::atomic<bool> *cancel,
                                          IoProgress progress)
{
    ImportResult result;
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
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
                int readRows = 0;
                while (query.next()) {
                    if (++readRows % 256 == 0 && isCanceled())
                        break; // the isCanceled() check below aborts before any import
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
    if (isCanceled()) {
        result.error = tr("Import canceled before it started — nothing was imported.");
        return result;
    }

    // One transaction and one refresh for the whole import.
    const bool bulk = m_storage->beginBulk();
    const int klipperTotal = records.size();
    int klipperDone = 0;
    for (const ClipboardRecord &record : records) {
        if (isCanceled()) {
            if (bulk)
                m_storage->endBulk(false); // roll back: a canceled import imports nothing
            result.error = tr("Import canceled after %1 of %2 entries — nothing was imported.")
                               .arg(klipperDone)
                               .arg(klipperTotal);
            return result;
        }
        ++klipperDone;
        if (klipperDone % 64 == 0 && progress)
            progress(klipperDone, klipperTotal);
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
    if (progress)
        progress(klipperDone, klipperTotal);
    if (bulk)
        m_storage->endBulk(true);

    emit m_storage->storageReset();
    result.ok = true;
    return result;
}

bool ExportImportManager::previewCopyQFile(const QString &path, CopyQPreview *preview,
                                           QString *error)
{
    if (!preview) {
        if (error)
            *error = tr("No CopyQ preview destination was provided.");
        return false;
    }
    QVector<CopyQImport::Tab> tabs;
    QString parseError;
    if (!CopyQImport::readTabs(path, &tabs, &parseError)) {
        if (error)
            *error = parseError;
        return false;
    }
    preview->tabs.clear();
    preview->itemsPerTab.clear();
    preview->totalItems = 0;
    for (const CopyQImport::Tab &tab : tabs) {
        preview->tabs.append(tab.name);
        preview->itemsPerTab.insert(tab.name, tab.items.size());
        preview->totalItems += tab.items.size();
    }
    return true;
}

ExportImportManager::ImportResult ExportImportManager::importCopyQFile(
    const QString &path, ImportMode mode, std::atomic<bool> *cancel, IoProgress progress)
{
    ImportResult result;
    const auto isCanceled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (isCanceled()) {
        result.error = tr("Import canceled before it started — nothing was imported.");
        return result;
    }
    QVector<CopyQImport::Tab> tabs;
    QString parseError;
    if (!CopyQImport::readTabs(path, &tabs, &parseError)) {
        result.error = parseError;
        return result;
    }
    struct Pending {
        ClipboardRecord record;
    };
    QVector<Pending> pending;
    int totalRows = 0;
    for (const CopyQImport::Tab &tab : tabs)
        totalRows += tab.items.size();
    pending.reserve(totalRows);
    const qint64 baseMs = QDateTime::currentMSecsSinceEpoch();
    int rowIndex = 0;
    for (const CopyQImport::Tab &tab : tabs) {
        for (const QVariantMap &data : tab.items) {
            ClipboardRecord record;
            // First row keeps the newest timestamp so file order survives.
            record.timestamp = qMax<qint64>(1, baseMs - rowIndex);
            record.sourceApp = QStringLiteral("copyq");
            record.sourceWindow = tab.name.left(512);
            ++rowIndex;
            QString skipReason;
            if (!CopyQImport::toRecord(data, &record, &skipReason)) {
                ++result.entriesSkipped;
                continue;
            }
            // Local-trust re-flag: sensitive-looking imports are marked even
            // though the payload itself is kept verbatim.
            if ((record.type == ContentType::Text || record.type == ContentType::RichText
                 || record.type == ContentType::Files)
                && SensitiveDataDetector::isSensitive(record.textData))
                record.sensitive = true;
            Pending item;
            item.record = record;
            pending.append(item);
        }
    }
    if (isCanceled()) {
        result.error = tr("Import canceled before it started — nothing was imported.");
        return result;
    }

    // CopyQ carries history only (tabs are flattened, groups have no mapping):
    // Overwrite replaces the history and keeps the user's groups/tags.
    const bool bulk = m_storage->beginBulk();
    if (mode == ImportMode::Overwrite)
        m_storage->clearHistory(true);
    const int total = pending.size() + result.entriesSkipped;
    int done = 0;
    // Hoisted: the duplicate probe is one prepared statement run per pending
    // row instead of a fresh prepare each time.
    QSqlDatabase db = m_storage->database();
    QSqlQuery duplicateProbe(db);
    duplicateProbe.prepare(QStringLiteral("SELECT id FROM entries WHERE content_hash = :h LIMIT 1"));
    const auto existsFor = [&duplicateProbe](const QByteArray &hash) -> std::optional<qint64> {
        duplicateProbe.bindValue(QStringLiteral(":h"), QString::fromLatin1(hash));
        if (duplicateProbe.exec() && duplicateProbe.next())
            return duplicateProbe.value(0).toLongLong();
        return std::nullopt;
    };
    for (const Pending &item : pending) {
        if (isCanceled()) {
            if (bulk)
                m_storage->endBulk(false); // roll back: a canceled import imports nothing
            result.error = tr("Import canceled after %1 of %2 entries — nothing was imported.")
                               .arg(done)
                               .arg(total);
            return result;
        }
        ++done;
        if (done % 64 == 0 && progress)
            progress(done, total);
        if (mode == ImportMode::SkipDuplicates) {
            if (existsFor(item.record.hash).has_value()) {
                ++result.entriesSkipped;
                continue;
            }
        }
        bool updatedExisting = false;
        if (m_storage->insertOrUpdate(item.record, &updatedExisting) == 0) {
            ++result.entriesSkipped;
            continue;
        }
        if (updatedExisting)
            ++result.entriesMerged;
        else
            ++result.entriesImported;
    }
    if (progress)
        progress(done, total);
    if (bulk)
        m_storage->endBulk(true);

    emit m_storage->storageReset(); // one reload for the whole import
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
        const QFileInfo info(files.at(i));
        const bool deleted = info.isDir() ? QDir(files.at(i)).removeRecursively()
                                          : QFile::remove(files.at(i));
        if (deleted)
            ++removed;
    }
    return removed;
}

ExportImportManager::BackupResult ExportImportManager::writeBackup(const QString &folder, int keep,
                                                                   std::atomic<bool> *cancel,
                                                                   IoProgress progress)
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
    // Backups hold secrets: the folder must not be world-readable.
    QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (cancel && cancel->load(std::memory_order_relaxed)) {
        result.error = tr("Backup canceled before it started — no file was written.");
        return result;
    }

    // Millisecond precision plus a zero-padded sequence keeps name order
    // chronological even when several backups land in the same millisecond.
    // NewOnly through the write (no exists-then-truncate window): a predicted
    // symlink must not clobber an arbitrary user file.
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QString path;
    QFile probe;
    bool reserved = false;
    for (int sequence = 1; sequence < 10000; ++sequence) {
        path = dir.filePath(QString::fromLatin1(kBackupPrefix) + stamp
                            + QStringLiteral("-%1.json").arg(sequence, 3, 10, QLatin1Char('0')));
        probe.setFileName(path);
        if (probe.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            reserved = true;
            break;
        }
        if (probe.error() != QFile::FileError::OpenError)
            break;
    }
    if (!reserved) {
        result.error = tr("Cannot create a backup file in %1: %2").arg(folder, probe.errorString());
        return result;
    }
    probe.close();
    // The NewOnly reservation above proved this path is our own inode (not a
    // planted symlink); exportToFile truncates that same file below.
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);

    ExportRequest request;
    request.scope = Scope::Everything;
    request.path = path;
    request.includeSensitive = true; // backups restore faithfully; 0600 + owned dir
    QString error;
    if (!exportToFile(request, &error, cancel, progress)) {
        result.error = error;
        QFile::remove(path); // cancel/failure writes no file
        return result;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);

    result.ok = true;
    result.path = path;
    result.pruned = pruneBackups(folder, keep);
    return result;
}
