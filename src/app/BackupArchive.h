#pragma once

#include <QString>

#include <atomic>
#include <functional>

class ExportImportManager;
class QSqlDatabase;
class StorageManager;

namespace BackupArchive {

using Progress = std::function<void(int done, int total)>;

struct Package {
    QString payloadPath;
    QByteArray settingsJson;
    bool payloadIsDatabase = false;
    bool databaseEncrypted = false;
};

bool zipAvailable();
bool databaseSnapshotAvailable();
bool isBundlePath(const QString &path);

bool write(StorageManager *storage, ExportImportManager *io, const QString &folder, int keep,
           const QByteArray &settingsJson, const QString &encryptionKey,
           std::atomic<bool> *cancel, Progress progress, QString *path, QString *error);

// A ZIP payload is streamed to destinationFolder after the manifest, entry
// names, sizes, and hashes have been checked. Multi-file fallback bundles are
// verified in place and return their history JSON path.
bool extract(const QString &path, const QString &destinationFolder,
             std::atomic<bool> *cancel, Progress progress, Package *package, QString *error);

bool restoreDatabase(const QSqlDatabase &sourceDatabase, const QSqlDatabase &targetDatabase,
                     std::atomic<bool> *cancel, Progress progress, QString *error);

} // namespace BackupArchive
