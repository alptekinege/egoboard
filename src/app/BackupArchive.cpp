#include "BackupArchive.h"

#include "ExportImportManager.h"
#include "SettingsManager.h"
#include "StorageManager.h"

#include <QCoreApplication>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QVariant>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#ifdef EGOBOARD_HAVE_SQLITE_BACKUP
#include <sqlite3.h>
#endif

#ifdef EGOBOARD_HAVE_ZIP_BACKUPS
#include <zip.h>
#include <sys/stat.h>
#endif

namespace BackupArchive {
namespace {

constexpr auto kBundleFormat = "egoboard-backup";
constexpr int kBundleVersion = 1;
constexpr qint64 kManifestLimitBytes = 1024 * 1024;
constexpr qint64 kSettingsLimitBytes = 8 * 1024 * 1024;
constexpr quint64 kPayloadLimitBytes = 32ULL * 1024 * 1024 * 1024;
constexpr qint64 kIoChunkBytes = 256 * 1024;

struct ManifestEntry {
    qint64 size = 0;
    QString sha256;
};

QString backupStem()
{
    return QStringLiteral("egoboard-backup-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
}

bool canceled(std::atomic<bool> *cancel)
{
    return cancel && cancel->load(std::memory_order_relaxed);
}

bool writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QObject::tr("Cannot finish writing %1: %2").arg(path, file.errorString());
        return false;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool hashFile(const QString &path, ManifestEntry *entry, QString *error,
              std::atomic<bool> *cancel = nullptr, Progress progress = {})
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 done = 0;
    const qint64 total = file.size();
    while (!file.atEnd()) {
        if (canceled(cancel)) {
            if (error)
                *error = QObject::tr("Backup canceled — no completed backup was kept.");
            return false;
        }
        const QByteArray chunk = file.read(kIoChunkBytes);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            if (error)
                *error = QObject::tr("Cannot read %1: %2").arg(path, file.errorString());
            return false;
        }
        hash.addData(chunk);
        done += chunk.size();
        if (progress)
            progress(int(done / 1024), int(qMax<qint64>(1, total / 1024)));
    }
    entry->size = done;
    entry->sha256 = QString::fromLatin1(hash.result().toHex());
    return true;
}

QJsonObject manifestFor(const QString &payloadName, const QString &payloadFormat,
                        bool databaseEncrypted, const QMap<QString, ManifestEntry> &entries)
{
    QJsonObject entryObject;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        QJsonObject metadata;
        metadata.insert(QStringLiteral("size"), double(it->size));
        metadata.insert(QStringLiteral("sha256"), it->sha256);
        entryObject.insert(it.key(), metadata);
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QString::fromLatin1(kBundleFormat));
    manifest.insert(QStringLiteral("version"), kBundleVersion);
    manifest.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("payload"), payloadName);
    manifest.insert(QStringLiteral("payloadFormat"), payloadFormat);
    manifest.insert(QStringLiteral("databaseEncrypted"), databaseEncrypted);
    manifest.insert(QStringLiteral("entries"), entryObject);
    return manifest;
}

bool parseManifest(const QByteArray &bytes, QJsonObject *manifest, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QObject::tr("The backup manifest is malformed.");
        return false;
    }
    *manifest = document.object();
    if (manifest->value(QStringLiteral("format")).toString() != QString::fromLatin1(kBundleFormat)
        || manifest->value(QStringLiteral("version")).toInt() != kBundleVersion) {
        if (error)
            *error = QObject::tr("The backup manifest format or version is not supported.");
        return false;
    }
    const QString payload = manifest->value(QStringLiteral("payload")).toString();
    const QString payloadFormat = manifest->value(QStringLiteral("payloadFormat")).toString();
    const bool expectedPayload = (payload == QLatin1String("database.sqlite")
                                  && payloadFormat == QLatin1String("sqlite"))
        || (payload == QLatin1String("history.json")
            && payloadFormat == QLatin1String("egoboard-json"));
    if (!expectedPayload || !manifest->value(QStringLiteral("entries")).isObject()) {
        if (error)
            *error = QObject::tr("The backup manifest does not describe a supported payload.");
        return false;
    }
    const QJsonObject entries = manifest->value(QStringLiteral("entries")).toObject();
    if (entries.size() != 2 || !entries.contains(payload)
        || !entries.contains(QStringLiteral("settings.json"))) {
        if (error)
            *error = QObject::tr("The backup manifest is missing required files.");
        return false;
    }
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const QJsonObject metadata = it.value().toObject();
        const double size = metadata.value(QStringLiteral("size")).toDouble(-1.0);
        const QString digest = metadata.value(QStringLiteral("sha256")).toString();
        if (!metadata.value(QStringLiteral("size")).isDouble() || size < 0.0
            || size > double(kPayloadLimitBytes) || digest.size() != 64
            || !std::all_of(digest.cbegin(), digest.cend(), [](QChar c) {
                   return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                       || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
               })) {
            if (error)
                *error = QObject::tr("The backup manifest contains invalid file metadata.");
            return false;
        }
    }
    return true;
}

bool verifySettings(const QByteArray &settingsJson, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(settingsJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QObject::tr("The settings file in this backup is malformed.");
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != SettingsManager::settingsFormatTag()
        || root.value(QStringLiteral("version")).toInt(-1) < 0
        || root.value(QStringLiteral("version")).toInt()
            > SettingsManager::settingsFormatVersion()) {
        if (error)
            *error = QObject::tr("The settings file in this backup is not supported.");
        return false;
    }
    return true;
}

#ifdef EGOBOARD_HAVE_SQLITE_BACKUP
bool executeSql(sqlite3 *db, const QByteArray &sql, QString *error)
{
    char *message = nullptr;
    const int rc = sqlite3_exec(db, sql.constData(), nullptr, nullptr, &message);
    if (rc == SQLITE_OK)
        return true;
    if (error) {
        *error = QObject::tr("Cannot snapshot the database: %1")
                     .arg(QString::fromUtf8(message ? message : sqlite3_errmsg(db)));
    }
    sqlite3_free(message);
    return false;
}

bool sqliteHandle(const QSqlDatabase &database, sqlite3 **handle, QString *error)
{
    const QVariant driverHandle = database.driver()->handle();
    if (!driverHandle.isValid() || qstrcmp(driverHandle.typeName(), "sqlite3*") != 0) {
        if (error)
            *error = QObject::tr("The SQLite driver does not expose a backup handle.");
        return false;
    }
    *handle = *static_cast<sqlite3 *const *>(driverHandle.constData());
    if (!*handle) {
        if (error)
            *error = QObject::tr("The history database connection is unavailable.");
        return false;
    }
    return true;
}

bool copyDatabase(sqlite3 *source, sqlite3 *destination, std::atomic<bool> *cancel,
                  Progress progress, QString *error)
{
    sqlite3_backup *backup = sqlite3_backup_init(destination, "main", source, "main");
    if (!backup) {
        if (error)
            *error = QObject::tr("Cannot start the database transfer: %1")
                         .arg(QString::fromUtf8(sqlite3_errmsg(destination)));
        return false;
    }
    bool ok = true;
    for (;;) {
        if (canceled(cancel)) {
            ok = false;
            if (error)
                *error = QObject::tr("Database transfer canceled; the current history was not changed.");
            break;
        }
        const int rc = sqlite3_backup_step(backup, 128);
        const int total = qMax(1, sqlite3_backup_pagecount(backup));
        const int done = qMax(0, total - sqlite3_backup_remaining(backup));
        if (progress)
            progress(done, total);
        if (rc == SQLITE_DONE)
            break;
        if (rc == SQLITE_OK)
            continue;
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            sqlite3_sleep(10);
            continue;
        }
        ok = false;
        if (error)
            *error = QObject::tr("Cannot copy the database: %1")
                         .arg(QString::fromUtf8(sqlite3_errmsg(destination)));
        break;
    }
    const int finishRc = sqlite3_backup_finish(backup);
    if (ok && finishRc != SQLITE_OK) {
        ok = false;
        if (error)
            *error = QObject::tr("Cannot finish the database transfer: %1")
                         .arg(QString::fromUtf8(sqlite3_errmsg(destination)));
    }
    return ok;
}

bool snapshotDatabase(QSqlDatabase sourceDb, const QString &path, const QString &key,
                      bool encrypted, std::atomic<bool> *cancel, Progress progress,
                      QString *error)
{
    sqlite3 *source = nullptr;
    if (!sqliteHandle(sourceDb, &source, error))
        return false;

    sqlite3 *destination = nullptr;
    const QByteArray nativePath = QFile::encodeName(path);
    int rc = sqlite3_open_v2(nativePath.constData(), &destination,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        if (error)
            *error = QObject::tr("Cannot create the database snapshot: %1")
                         .arg(QString::fromUtf8(destination ? sqlite3_errmsg(destination)
                                                           : sqlite3_errstr(rc)));
        if (destination)
            sqlite3_close(destination);
        return false;
    }

    if (encrypted) {
        if (key.isEmpty()) {
            if (error)
                *error = QObject::tr("The encrypted database key is unavailable in KWallet.");
            sqlite3_close(destination);
            return false;
        }
        const QByteArray keyHex = key.toUtf8().toHex();
        if (!executeSql(destination, QByteArrayLiteral("PRAGMA key = x'") + keyHex
                                          + QByteArrayLiteral("'"), error)) {
            sqlite3_close(destination);
            return false;
        }
        sqlite3_stmt *cipherQuery = nullptr;
        rc = sqlite3_prepare_v2(destination, "PRAGMA cipher_version", -1, &cipherQuery, nullptr);
        const bool cipherReady = rc == SQLITE_OK && cipherQuery
            && sqlite3_step(cipherQuery) == SQLITE_ROW
            && sqlite3_column_text(cipherQuery, 0)
            && sqlite3_column_bytes(cipherQuery, 0) > 0;
        if (cipherQuery)
            sqlite3_finalize(cipherQuery);
        if (!cipherReady) {
            if (error)
                *error = QObject::tr("This build cannot preserve SQLCipher encryption in ZIP backups.");
            sqlite3_close(destination);
            return false;
        }
    }

    bool ok = copyDatabase(source, destination, cancel, progress, error);
    const int closeRc = sqlite3_close(destination);
    if (ok && closeRc != SQLITE_OK) {
        ok = false;
        if (error)
            *error = QObject::tr("Cannot close the database snapshot: %1")
                         .arg(QString::fromUtf8(sqlite3_errstr(closeRc)));
    }
    if (!ok) {
        QFile::remove(path);
        return false;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}
#endif // EGOBOARD_HAVE_SQLITE_BACKUP

#ifdef EGOBOARD_HAVE_ZIP_BACKUPS
struct ZipProgressContext {
    std::atomic<bool> *cancel = nullptr;
    Progress *progress = nullptr;
};

void reportZipProgress(zip_t *, double fraction, void *opaque)
{
    auto *context = static_cast<ZipProgressContext *>(opaque);
    if (context && context->progress && *context->progress)
        (*context->progress)(qBound(0, qRound(fraction * 100.0), 100), 100);
}

int cancelZipWrite(zip_t *, void *opaque)
{
    const auto *context = static_cast<const ZipProgressContext *>(opaque);
    return context && canceled(context->cancel);
}

bool addZipFile(zip_t *archive, const QString &sourcePath, const char *memberName,
                QString *error)
{
    const QByteArray nativePath = QFile::encodeName(sourcePath);
    zip_source_t *source = zip_source_file(archive, nativePath.constData(), 0,
                                           ZIP_LENGTH_UNCHECKED);
    if (!source) {
        if (error)
            *error = QObject::tr("Cannot add %1 to the ZIP backup: %2")
                         .arg(QString::fromLatin1(memberName), QString::fromUtf8(zip_strerror(archive)));
        return false;
    }
    const zip_int64_t index = zip_file_add(archive, memberName, source, ZIP_FL_ENC_UTF_8);
    if (index < 0) {
        zip_source_free(source);
        if (error)
            *error = QObject::tr("Cannot add %1 to the ZIP backup: %2")
                         .arg(QString::fromLatin1(memberName), QString::fromUtf8(zip_strerror(archive)));
        return false;
    }
    if (zip_set_file_compression(archive, zip_uint64_t(index), ZIP_CM_DEFLATE, 6) != 0
        || zip_file_set_encryption(archive, zip_uint64_t(index), ZIP_EM_NONE, nullptr) != 0) {
        if (error)
            *error = QObject::tr("Cannot configure the ZIP backup member %1: %2")
                         .arg(QString::fromLatin1(memberName), QString::fromUtf8(zip_strerror(archive)));
        return false;
    }
    return true;
}

bool readZipEntry(zip_t *archive, const QString &name, const QString &outputPath,
                  quint64 expectedSize, const QByteArray &expectedHash, QByteArray *memory,
                  std::atomic<bool> *cancel, Progress progress, qint64 *aggregateDone,
                  qint64 aggregateTotal, QString *error)
{
    const QByteArray encodedName = name.toUtf8();
    const zip_int64_t index = zip_name_locate(archive, encodedName.constData(), ZIP_FL_ENC_GUESS);
    if (index < 0) {
        if (error)
            *error = QObject::tr("The backup is missing %1.").arg(name);
        return false;
    }
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(archive, zip_uint64_t(index), 0, &stat) != 0
        || !(stat.valid & ZIP_STAT_SIZE) || !(stat.valid & ZIP_STAT_ENCRYPTION_METHOD)
        || stat.size != expectedSize || stat.size > kPayloadLimitBytes
        || stat.encryption_method != ZIP_EM_NONE) {
        if (error)
            *error = QObject::tr("The backup member %1 has invalid size or encryption metadata.")
                         .arg(name);
        return false;
    }
    zip_uint8_t opsys = 0;
    zip_uint32_t attributes = 0;
    if (zip_file_get_external_attributes(archive, zip_uint64_t(index), 0, &opsys,
                                         &attributes) == 0
        && opsys == ZIP_OPSYS_UNIX) {
        const mode_t mode = mode_t(attributes >> 16);
        if (S_ISLNK(mode) || (mode != 0 && !S_ISREG(mode))) {
            if (error)
                *error = QObject::tr("The backup member %1 is not a regular file.").arg(name);
            return false;
        }
    }

    zip_file_t *input = zip_fopen_index(archive, zip_uint64_t(index), 0);
    if (!input) {
        if (error)
            *error = QObject::tr("Cannot read %1 from the backup: %2")
                         .arg(name, QString::fromUtf8(zip_strerror(archive)));
        return false;
    }
    QFile output;
    if (!outputPath.isEmpty()) {
        output.setFileName(outputPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            zip_fclose(input);
            if (error)
                *error = QObject::tr("Cannot create %1: %2").arg(outputPath, output.errorString());
            return false;
        }
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, size_t(kIoChunkBytes)> buffer{};
    quint64 totalRead = 0;
    bool ok = true;
    for (;;) {
        if (canceled(cancel)) {
            ok = false;
            if (error)
                *error = QObject::tr("Restore canceled — the current history was not changed.");
            break;
        }
        const zip_int64_t count = zip_fread(input, buffer.data(), buffer.size());
        if (count < 0) {
            ok = false;
            if (error)
                *error = QObject::tr("The backup member %1 is corrupt.").arg(name);
            break;
        }
        if (count == 0)
            break;
        totalRead += quint64(count);
        if (totalRead > expectedSize) {
            ok = false;
            if (error)
                *error = QObject::tr("The backup member %1 exceeds its manifest size.").arg(name);
            break;
        }
        hash.addData(QByteArrayView(buffer.data(), qsizetype(count)));
        if (memory)
            memory->append(buffer.data(), qsizetype(count));
        if (output.isOpen() && output.write(buffer.data(), count) != count) {
            ok = false;
            if (error)
                *error = QObject::tr("Cannot write the restored file %1: %2")
                             .arg(outputPath, output.errorString());
            break;
        }
        *aggregateDone += count;
        if (progress)
            progress(int(*aggregateDone / 1024), int(qMax<qint64>(1, aggregateTotal / 1024)));
    }
    const int closeRc = zip_fclose(input);
    if (closeRc != 0 && ok) {
        ok = false;
        if (error)
            *error = QObject::tr("The backup member %1 failed its ZIP integrity check.").arg(name);
    }
    if (output.isOpen()) {
        if (!output.flush()) {
            ok = false;
            if (error)
                *error = QObject::tr("Cannot finish writing %1: %2")
                             .arg(outputPath, output.errorString());
        }
        output.close();
        QFile::setPermissions(outputPath, QFile::ReadOwner | QFile::WriteOwner);
    }
    if (ok && (totalRead != expectedSize || hash.result().toHex() != expectedHash)) {
        ok = false;
        if (error)
            *error = QObject::tr("The backup member %1 does not match its manifest.").arg(name);
    }
    if (!ok && !outputPath.isEmpty())
        QFile::remove(outputPath);
    return ok;
}

#endif // EGOBOARD_HAVE_ZIP_BACKUPS

bool readSmallFile(const QString &path, qint64 limit, QByteArray *bytes, QString *error)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.size() < 0 || info.size() > limit) {
        if (error)
            *error = QObject::tr("The backup file %1 is not a regular file or is too large.")
                         .arg(path);
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    *bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        if (error)
            *error = QObject::tr("Cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool verifyFile(const QString &path, const QJsonObject &manifestEntry, QString *error,
                std::atomic<bool> *cancel, Progress progress)
{
    ManifestEntry actual;
    if (!hashFile(path, &actual, error, cancel, std::move(progress)))
        return false;
    if (actual.size != qint64(manifestEntry.value(QStringLiteral("size")).toDouble(-1.0))
        || actual.sha256.compare(manifestEntry.value(QStringLiteral("sha256")).toString(),
                                 Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QObject::tr("The backup file %1 does not match its manifest.")
                         .arg(QFileInfo(path).fileName());
        return false;
    }
    return true;
}

} // namespace

bool zipAvailable()
{
#ifdef EGOBOARD_HAVE_ZIP_BACKUPS
    return true;
#else
    return false;
#endif
}

bool databaseSnapshotAvailable()
{
#ifdef EGOBOARD_HAVE_SQLITE_BACKUP
    return true;
#else
    return false;
#endif
}

bool isBundlePath(const QString &path)
{
    const QFileInfo info(path);
    return (info.isDir() && info.suffix() == QLatin1String("backup"))
        || (info.isFile() && info.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0);
}

bool write(StorageManager *storage, ExportImportManager *io, const QString &folder, int keep,
           const QByteArray &settingsJson, const QString &encryptionKey,
           std::atomic<bool> *cancel, Progress progress, QString *path, QString *error)
{
    if (!storage || !io || folder.trimmed().isEmpty()) {
        if (error)
            *error = QObject::tr("No backup destination is available.");
        return false;
    }
    QDir directory(folder);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QObject::tr("Cannot create the backup folder %1.").arg(folder);
        return false;
    }
    QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (canceled(cancel)) {
        if (error)
            *error = QObject::tr("Backup canceled before it started — no file was written.");
        return false;
    }
    if (!verifySettings(settingsJson, error))
        return false;

    QTemporaryDir staging(directory.filePath(QStringLiteral(".egoboard-backup-XXXXXX")));
    if (!staging.isValid()) {
        if (error)
            *error = QObject::tr("Cannot create a private temporary backup folder in %1.")
                         .arg(folder);
        return false;
    }
    QFile::setPermissions(staging.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const bool encrypted = storage->isEncrypted();

#ifdef EGOBOARD_HAVE_SQLITE_BACKUP
    const QString payloadPath = staging.filePath(QStringLiteral("database.sqlite"));
    if (!snapshotDatabase(storage->database(), payloadPath, encryptionKey, encrypted,
                          cancel, progress, error))
        return false;
    const QString payloadName = QStringLiteral("database.sqlite");
    const QString payloadFormat = QStringLiteral("sqlite");
#else
    const QString payloadPath = staging.filePath(QStringLiteral("history.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = payloadPath;
    request.includeSensitive = true;
    QString exportError;
    if (!io->exportToFile(request, &exportError, cancel, progress)) {
        if (error)
            *error = exportError;
        return false;
    }
    QFile::setPermissions(payloadPath, QFile::ReadOwner | QFile::WriteOwner);
    const QString payloadName = QStringLiteral("history.json");
    const QString payloadFormat = QStringLiteral("egoboard-json");
#endif

    const QString settingsPath = staging.filePath(QStringLiteral("settings.json"));
    if (!writeFile(settingsPath, settingsJson, error))
        return false;

    QMap<QString, ManifestEntry> entries;
    ManifestEntry payloadEntry;
    if (!hashFile(payloadPath, &payloadEntry, error, cancel, progress))
        return false;
    entries.insert(payloadName, payloadEntry);
    ManifestEntry settingsEntry;
    if (!hashFile(settingsPath, &settingsEntry, error, cancel, progress))
        return false;
    entries.insert(QStringLiteral("settings.json"), settingsEntry);
    const QByteArray manifestBytes = QJsonDocument(
        manifestFor(payloadName, payloadFormat, encrypted, entries)).toJson(QJsonDocument::Compact);
    const QString manifestPath = staging.filePath(QStringLiteral("manifest.json"));
    if (!writeFile(manifestPath, manifestBytes, error))
        return false;

    const QString stem = backupStem();
    QString finalPath;
#ifdef EGOBOARD_HAVE_ZIP_BACKUPS
    QTemporaryFile archiveTemp(directory.filePath(QStringLiteral(".egoboard-backup-XXXXXX.zip")));
    if (!archiveTemp.open()) {
        if (error)
            *error = QObject::tr("Cannot create a temporary ZIP backup in %1: %2")
                         .arg(folder, archiveTemp.errorString());
        return false;
    }
    const QString archiveTempPath = archiveTemp.fileName();
    archiveTemp.close();
    QFile::setPermissions(archiveTempPath, QFile::ReadOwner | QFile::WriteOwner);
    int zipError = 0;
    const QByteArray nativeArchive = QFile::encodeName(archiveTempPath);
    zip_t *archive = zip_open(nativeArchive.constData(), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    if (!archive) {
        QFile::remove(archiveTempPath);
        if (error)
            *error = QObject::tr("Cannot create the ZIP backup (libzip error %1).").arg(zipError);
        return false;
    }
    if (!addZipFile(archive, payloadPath, "database.sqlite", error)
        || !addZipFile(archive, settingsPath, "settings.json", error)
        || !addZipFile(archive, manifestPath, "manifest.json", error)) {
        zip_discard(archive);
        QFile::remove(archiveTempPath);
        return false;
    }
    ZipProgressContext context{cancel, &progress};
    if (zip_register_progress_callback_with_state(archive, 0.02, &reportZipProgress,
                                                 nullptr, &context) != 0
        || zip_register_cancel_callback_with_state(archive, &cancelZipWrite,
                                                  nullptr, &context) != 0) {
        zip_discard(archive);
        QFile::remove(archiveTempPath);
        if (error)
            *error = QObject::tr("Cannot enable progress and cancellation for the ZIP backup.");
        return false;
    }
    if (zip_close(archive) != 0) {
        const QString zipErrorText = QString::fromUtf8(zip_strerror(archive));
        zip_discard(archive);
        QFile::remove(archiveTempPath);
        if (error)
            *error = canceled(cancel) ? QObject::tr("Backup canceled — no completed backup was kept.")
                                      : QObject::tr("Cannot finish the ZIP backup: %1").arg(zipErrorText);
        return false;
    }
    if (canceled(cancel)) {
        QFile::remove(archiveTempPath);
        if (error)
            *error = QObject::tr("Backup canceled — no completed backup was kept.");
        return false;
    }
    QFile::setPermissions(archiveTempPath, QFile::ReadOwner | QFile::WriteOwner);
    bool renamed = false;
    for (int sequence = 1; sequence < 10000; ++sequence) {
        finalPath = directory.filePath(stem + QStringLiteral("-%1.zip")
                                                  .arg(sequence, 3, 10, QLatin1Char('0')));
        if (QFile::rename(archiveTempPath, finalPath)) {
            renamed = true;
            break;
        }
        if (QFile::exists(archiveTempPath) && QFileInfo::exists(finalPath))
            continue;
        break;
    }
    if (!renamed) {
        QFile::remove(archiveTempPath);
        if (error)
            *error = QObject::tr("Cannot atomically install the ZIP backup in %1.").arg(folder);
        return false;
    }
#else
    Q_UNUSED(encryptionKey);
    if (canceled(cancel)) {
        if (error)
            *error = QObject::tr("Backup canceled — no completed backup was kept.");
        return false;
    }
    bool renamed = false;
    for (int sequence = 1; sequence < 10000; ++sequence) {
        finalPath = directory.filePath(stem + QStringLiteral("-%1.backup")
                                                  .arg(sequence, 3, 10, QLatin1Char('0')));
        if (!QFileInfo::exists(finalPath)
            && QDir().rename(staging.path(), finalPath)) {
            renamed = true;
            break;
        }
        if (QFileInfo::exists(finalPath))
            continue;
        break;
    }
    if (!renamed) {
        if (error)
            *error = QObject::tr("Cannot atomically install the multi-file backup in %1.")
                         .arg(folder);
        return false;
    }
#endif

    QFile::setPermissions(finalPath, QFileInfo(finalPath).isDir()
        ? QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
        : QFile::ReadOwner | QFile::WriteOwner);
    if (path)
        *path = finalPath;
    ExportImportManager::pruneBackups(folder, keep);
    return true;
}

bool extract(const QString &path, const QString &destinationFolder,
             std::atomic<bool> *cancel, Progress progress, Package *package, QString *error)
{
    if (!package) {
        if (error)
            *error = QObject::tr("No backup package destination was provided.");
        return false;
    }
    const QFileInfo backupInfo(path);
    if (backupInfo.isSymLink()) {
        if (error)
            *error = QObject::tr("A backup cannot be restored through a symbolic link.");
        return false;
    }
    if (backupInfo.isDir()) {
        const QString manifestPath = QDir(path).filePath(QStringLiteral("manifest.json"));
        QByteArray manifestBytes;
        if (!readSmallFile(manifestPath, kManifestLimitBytes, &manifestBytes, error))
            return false;
        QJsonObject manifest;
        if (!parseManifest(manifestBytes, &manifest, error))
            return false;
        const QString payload = manifest.value(QStringLiteral("payload")).toString();
        const QJsonObject entries = manifest.value(QStringLiteral("entries")).toObject();
        const QStringList expectedFiles{payload, QStringLiteral("settings.json"),
                                        QStringLiteral("manifest.json")};
        const QStringList actualFiles = QDir(path).entryList(QDir::Files | QDir::NoDotAndDotDot,
                                                             QDir::Name);
        if (actualFiles.size() != expectedFiles.size()) {
            if (error)
                *error = QObject::tr("The multi-file backup contains unexpected files.");
            return false;
        }
        for (const QString &file : expectedFiles) {
            if (!actualFiles.contains(file)) {
                if (error)
                    *error = QObject::tr("The multi-file backup is missing %1.").arg(file);
                return false;
            }
        }
        const QString payloadPath = QDir(path).filePath(payload);
        const QString settingsPath = QDir(path).filePath(QStringLiteral("settings.json"));
        if (!verifyFile(payloadPath, entries.value(payload).toObject(), error, cancel, progress)
            || !verifyFile(settingsPath, entries.value(QStringLiteral("settings.json")).toObject(),
                           error, cancel, progress))
            return false;
        if (canceled(cancel)) {
            if (error)
                *error = QObject::tr("Restore canceled — the current history was not changed.");
            return false;
        }
        QByteArray settingsJson;
        if (!readSmallFile(settingsPath, kSettingsLimitBytes, &settingsJson, error)
            || !verifySettings(settingsJson, error))
            return false;
        package->payloadPath = payloadPath;
        package->payloadIsDatabase = payload == QLatin1String("database.sqlite");
        package->databaseEncrypted = manifest.value(QStringLiteral("databaseEncrypted")).toBool();
        package->settingsJson = settingsJson;
        return true;
    }

#ifndef EGOBOARD_HAVE_ZIP_BACKUPS
    Q_UNUSED(destinationFolder);
    Q_UNUSED(cancel);
    Q_UNUSED(progress);
    if (error)
        *error = QObject::tr("ZIP backup support is not available in this build.");
    return false;
#else
    const QByteArray nativePath = QFile::encodeName(path);
    int zipError = 0;
    zip_t *archive = zip_open(nativePath.constData(), ZIP_RDONLY, &zipError);
    if (!archive) {
        if (error)
            *error = QObject::tr("Cannot open the ZIP backup (libzip error %1).").arg(zipError);
        return false;
    }
    const zip_int64_t count = zip_get_num_entries(archive, 0);
    if (count != 3) {
        zip_discard(archive);
        if (error)
            *error = QObject::tr("The ZIP backup must contain exactly three files.");
        return false;
    }
    QSet<QString> names;
    for (zip_uint64_t i = 0; i < zip_uint64_t(count); ++i) {
        const char *name = zip_get_name(archive, i, ZIP_FL_ENC_GUESS);
        if (!name) {
            zip_discard(archive);
            if (error)
                *error = QObject::tr("The ZIP backup contains an invalid filename.");
            return false;
        }
        names.insert(QString::fromUtf8(name));
    }
    if (names.size() != 3 || !names.contains(QStringLiteral("manifest.json"))
        || !names.contains(QStringLiteral("settings.json"))
        || (!names.contains(QStringLiteral("database.sqlite"))
            && !names.contains(QStringLiteral("history.json")))) {
        zip_discard(archive);
        if (error)
            *error = QObject::tr("The ZIP backup contains unexpected files.");
        return false;
    }

    QByteArray manifestBytes;
    zip_stat_t manifestStat;
    zip_stat_init(&manifestStat);
    const zip_int64_t manifestIndex = zip_name_locate(archive, "manifest.json", 0);
    if (manifestIndex < 0 || zip_stat_index(archive, zip_uint64_t(manifestIndex), 0,
                                            &manifestStat) != 0
        || !(manifestStat.valid & ZIP_STAT_SIZE) || manifestStat.size > quint64(kManifestLimitBytes)
        || manifestStat.encryption_method != ZIP_EM_NONE) {
        zip_discard(archive);
        if (error)
            *error = QObject::tr("The ZIP backup manifest is missing or too large.");
        return false;
    }
    zip_file_t *manifestFile = zip_fopen_index(archive, zip_uint64_t(manifestIndex), 0);
    if (!manifestFile) {
        zip_discard(archive);
        if (error)
            *error = QObject::tr("Cannot read the ZIP backup manifest.");
        return false;
    }
    std::array<char, 8192> manifestBuffer{};
    bool manifestOk = true;
    for (;;) {
        const zip_int64_t bytes = zip_fread(manifestFile, manifestBuffer.data(), manifestBuffer.size());
        if (bytes < 0) {
            manifestOk = false;
            break;
        }
        if (bytes == 0)
            break;
        manifestBytes.append(manifestBuffer.data(), qsizetype(bytes));
    }
    if (zip_fclose(manifestFile) != 0)
        manifestOk = false;
    QJsonObject manifest;
    if (!manifestOk || manifestBytes.size() != qint64(manifestStat.size)
        || !parseManifest(manifestBytes, &manifest, error)) {
        zip_discard(archive);
        if (error && error->isEmpty())
            *error = QObject::tr("The ZIP backup manifest is corrupt.");
        return false;
    }
    const QString payload = manifest.value(QStringLiteral("payload")).toString();
    const bool payloadIsDatabase = payload == QLatin1String("database.sqlite");
    const QJsonObject entries = manifest.value(QStringLiteral("entries")).toObject();
    const QString expectedPayload = payloadIsDatabase ? QStringLiteral("database.sqlite")
                                                      : QStringLiteral("history.json");
    if (!names.contains(expectedPayload)) {
        zip_discard(archive);
        if (error)
            *error = QObject::tr("The ZIP member list does not match the manifest.");
        return false;
    }
    const auto metadataFor = [&entries](const QString &name, quint64 *size, QByteArray *hash) {
        const QJsonObject metadata = entries.value(name).toObject();
        const double rawSize = metadata.value(QStringLiteral("size")).toDouble(-1.0);
        const QString digest = metadata.value(QStringLiteral("sha256")).toString();
        if (!metadata.value(QStringLiteral("size")).isDouble() || rawSize < 0.0
            || rawSize > double(kPayloadLimitBytes) || digest.size() != 64)
            return false;
        *size = quint64(rawSize);
        *hash = digest.toLatin1().toLower();
        return true;
    };
    quint64 payloadSize = 0;
    quint64 settingsSize = 0;
    QByteArray payloadHash;
    QByteArray settingsHash;
    if (!metadataFor(expectedPayload, &payloadSize, &payloadHash)
        || !metadataFor(QStringLiteral("settings.json"), &settingsSize, &settingsHash)
        || settingsSize > quint64(kSettingsLimitBytes)) {
        zip_discard(archive);
        if (error)
            *error = QObject::tr("The ZIP backup manifest contains invalid member metadata.");
        return false;
    }

    zip_int64_t totalRaw = zip_int64_t(payloadSize + settingsSize);
    qint64 totalDone = 0;
    const QString payloadOut = QDir(destinationFolder).filePath(expectedPayload);
    if (payloadIsDatabase) {
        if (!readZipEntry(archive, expectedPayload, payloadOut, payloadSize, payloadHash,
                          nullptr, cancel, progress, &totalDone, totalRaw, error)) {
            zip_discard(archive);
            return false;
        }
    } else {
        QByteArray historyJson;
        if (payloadSize > quint64(std::numeric_limits<qsizetype>::max())
            || !readZipEntry(archive, expectedPayload, {}, payloadSize, payloadHash,
                             &historyJson, cancel, progress, &totalDone, totalRaw, error)) {
            zip_discard(archive);
            return false;
        }
        const QString historyPath = QDir(destinationFolder).filePath(expectedPayload);
        if (!writeFile(historyPath, historyJson, error)) {
            zip_discard(archive);
            return false;
        }
        package->payloadPath = historyPath;
    }
    QByteArray settingsJson;
    if (!readZipEntry(archive, QStringLiteral("settings.json"), {}, settingsSize, settingsHash,
                      &settingsJson, cancel, progress, &totalDone, totalRaw, error)) {
        zip_discard(archive);
        return false;
    }
    zip_discard(archive);
    if (!verifySettings(settingsJson, error))
        return false;
    package->payloadPath = payloadIsDatabase
        ? payloadOut : QDir(destinationFolder).filePath(expectedPayload);
    package->payloadIsDatabase = payloadIsDatabase;
    package->databaseEncrypted = manifest.value(QStringLiteral("databaseEncrypted")).toBool();
    package->settingsJson = settingsJson;
    return true;
#endif
}

bool restoreDatabase(const QSqlDatabase &sourceDatabase, const QSqlDatabase &targetDatabase,
                     std::atomic<bool> *cancel, Progress progress, QString *error)
{
#ifdef EGOBOARD_HAVE_SQLITE_BACKUP
    sqlite3 *source = nullptr;
    sqlite3 *target = nullptr;
    if (!sqliteHandle(sourceDatabase, &source, error)
        || !sqliteHandle(targetDatabase, &target, error))
        return false;
    return copyDatabase(source, target, cancel, std::move(progress), error);
#else
    Q_UNUSED(sourceDatabase);
    Q_UNUSED(targetDatabase);
    Q_UNUSED(cancel);
    Q_UNUSED(progress);
    if (error)
        *error = QObject::tr("Native SQLite database restore is not available in this build.");
    return false;
#endif
}

} // namespace BackupArchive
