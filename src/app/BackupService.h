#pragma once

#include "ExportImportManager.h"

#include <QObject>

#include <functional>
#include <memory>

class BookmarkManager;
class QThread;
class QTimer;
class SettingsManager;
class SnippetManager;
class StorageManager;

// Performs one JSON backup — or a restore — with its own database connection
// on a worker thread (the GUI connection belongs to the GUI thread), mirroring
// VacuumWorker.
class BackupWorker : public QObject {
    Q_OBJECT
public:
    explicit BackupWorker(const QString &databasePath, QObject *parent = nullptr);
    // Out of line: the owned types are incomplete in this header.
    ~BackupWorker() override;

    // Schedules a backup on the worker's thread; safe to call from the GUI.
    void requestRun(const QString &folder, int keep, const QString &encryptionKey);
    // Schedules a restore of a backup file; safe to call from the GUI.
    void requestRestore(const QString &path, ExportImportManager::ImportMode mode,
                        const QString &encryptionKey);

signals:
    void finished(bool ok, const QString &path, const QString &error);
    void restoreFinished(bool ok, const QString &path, const QString &error, int imported,
                         int merged, int skipped);

private:
    void run(const QString &folder, int keep, const QString &encryptionKey);
    void restore(const QString &path, ExportImportManager::ImportMode mode,
                 const QString &encryptionKey);
    // Opens the worker-side connection on first use; false on failure.
    bool ensureStorage(const QString &encryptionKey);

    QString m_databasePath;
    // Created lazily on the worker thread so the connection lives there.
    std::unique_ptr<StorageManager> m_storage;
    std::unique_ptr<BookmarkManager> m_bookmarks;
    std::unique_ptr<SnippetManager> m_snippets;
    std::unique_ptr<ExportImportManager> m_io;
};

// Automatic backups: runs daily (and once at startup when overdue), writes
// egoboard-backup-*.json into the configured folder and prunes old files.
// Also restores a backup file on request.
class BackupService : public QObject {
    Q_OBJECT
public:
    BackupService(const QString &databasePath, SettingsManager *settings, QObject *parent = nullptr);
    ~BackupService() override;

    // Starts the daily timer and catches up if the last run is overdue.
    void start();
    // Runs a backup now; false when one is already running or none is possible.
    bool runNow();
    // Restores a backup file (on the worker thread); false when busy.
    bool restoreNow(const QString &path, ExportImportManager::ImportMode mode);
    bool isRunning() const { return m_running; }
    // Effective folder (configured value, else the default under Documents).
    QString folder() const;
    // Supplies the SQLCipher key for encrypted databases (read from KWallet).
    void setKeyProvider(std::function<QString()> provider) { m_keyProvider = std::move(provider); }

signals:
    void finished(bool ok, const QString &path, const QString &error);
    // Emitted after a restore; the caller refreshes storage/models.
    void restoreFinished(bool ok, const QString &path, const QString &error, int imported,
                         int merged, int skipped);

private:
    void schedule();
    void maybeRunCatchUp();
    QString walletKey() const;

    QString m_databasePath;
    SettingsManager *m_settings = nullptr;
    QThread *m_thread = nullptr;
    BackupWorker *m_worker = nullptr;
    QTimer *m_timer = nullptr;
    std::function<QString()> m_keyProvider;
    bool m_running = false;
};
