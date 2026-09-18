#include "BackupService.h"

#include "BookmarkManager.h"
#include "ExportImportManager.h"
#include "SettingsManager.h"
#include "SnippetManager.h"
#include "StorageManager.h"

#include <QDateTime>
#include <QThread>
#include <QTimer>

namespace {
// Backups are daily; the startup catch-up uses the same interval.
constexpr qint64 kBackupIntervalMs = 24 * 60 * 60 * 1000;
} // namespace

BackupWorker::BackupWorker(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_databasePath(databasePath)
{
}

BackupWorker::~BackupWorker() = default;

void BackupWorker::requestRun(const QString &folder, int keep, const QString &encryptionKey)
{
    if (QThread::currentThread() == thread()) {
        run(folder, keep, encryptionKey);
        return;
    }
    QMetaObject::invokeMethod(
        this, [this, folder, keep, encryptionKey] { run(folder, keep, encryptionKey); },
        Qt::QueuedConnection);
}

void BackupWorker::requestRestore(const QString &path, ExportImportManager::ImportMode mode,
                                  const QString &encryptionKey)
{
    if (QThread::currentThread() == thread()) {
        restore(path, mode, encryptionKey);
        return;
    }
    QMetaObject::invokeMethod(
        this, [this, path, mode, encryptionKey] { restore(path, mode, encryptionKey); },
        Qt::QueuedConnection);
}

bool BackupWorker::ensureStorage(const QString &encryptionKey)
{
    if (m_storage)
        return true;
    // First use on this thread: create the connection here so every run reuses
    // it without touching the GUI connection.
    m_storage = std::make_unique<StorageManager>(m_databasePath);
    if (!m_storage->database().isOpen()) {
        m_storage.reset();
        return false;
    }
    if (!encryptionKey.isEmpty() && !m_storage->setEncryptionKey(encryptionKey)) {
        m_storage.reset();
        return false;
    }
    m_bookmarks = std::make_unique<BookmarkManager>(m_storage->database());
    m_snippets = std::make_unique<SnippetManager>(m_storage->database());
    m_io = std::make_unique<ExportImportManager>(m_storage.get(), m_bookmarks.get(),
                                                 m_snippets.get());
    return true;
}

void BackupWorker::run(const QString &folder, int keep, const QString &encryptionKey)
{
    if (!ensureStorage(encryptionKey)) {
        const bool locked = !encryptionKey.isEmpty();
        emit finished(false, {},
                      locked ? tr("Cannot unlock the encrypted history database.")
                             : tr("Cannot open the history database."));
        return;
    }

    const ExportImportManager::BackupResult result = m_io->writeBackup(folder, keep);
    emit finished(result.ok, result.path, result.error);
}

void BackupWorker::restore(const QString &path, ExportImportManager::ImportMode mode,
                           const QString &encryptionKey)
{
    if (!ensureStorage(encryptionKey)) {
        const bool locked = !encryptionKey.isEmpty();
        emit restoreFinished(false, path,
                             locked ? tr("Cannot unlock the encrypted history database.")
                                    : tr("Cannot open the history database."),
                             0, 0, 0);
        return;
    }

    const ExportImportManager::ImportResult result = m_io->importFromFile(path, mode);
    emit restoreFinished(result.ok, path, result.error, result.entriesImported,
                         result.entriesMerged, result.entriesSkipped);
}

BackupService::BackupService(const QString &databasePath, SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_databasePath(databasePath)
    , m_settings(settings)
{
}

BackupService::~BackupService()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
    }
}

void BackupService::start()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kBackupIntervalMs);
        connect(m_timer, &QTimer::timeout, this, [this] { runNow(); });
        connect(m_settings, &SettingsManager::changed, this, [this] { schedule(); });
    }
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new BackupWorker(m_databasePath);
        m_worker->moveToThread(m_thread);
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &BackupWorker::finished, this,
                [this](bool ok, const QString &path, const QString &error) {
                    m_running = false;
                    if (ok)
                        m_settings->setLastBackupMs(QDateTime::currentMSecsSinceEpoch());
                    emit finished(ok, path, error);
                    schedule();
                });
        connect(m_worker, &BackupWorker::restoreFinished, this,
                [this](bool ok, const QString &path, const QString &error, int imported,
                       int merged, int skipped) {
                    m_running = false;
                    emit restoreFinished(ok, path, error, imported, merged, skipped);
                    schedule();
                });
        m_thread->start();
    }

    schedule();
    maybeRunCatchUp();
}

QString BackupService::folder() const
{
    const QString configured = m_settings->backupFolder();
    return configured.isEmpty() ? SettingsManager::defaultBackupFolder() : configured;
}

bool BackupService::runNow()
{
    // Deliberately allowed while the daily schedule is off: the settings dialog
    // offers a manual "Back up now".
    if (!m_worker || m_running)
        return false;
    m_running = true;
    m_worker->requestRun(folder(), m_settings->backupKeep(), walletKey());
    return true;
}

bool BackupService::restoreNow(const QString &path, ExportImportManager::ImportMode mode)
{
    if (!m_worker || m_running || path.trimmed().isEmpty())
        return false;
    m_running = true;
    m_worker->requestRestore(path.trimmed(), mode, walletKey());
    return true;
}

QString BackupService::walletKey() const
{
    return m_keyProvider ? m_keyProvider() : QString();
}

void BackupService::schedule()
{
    if (!m_timer)
        return;
    if (m_settings->backupsEnabled()) {
        if (!m_timer->isActive())
            m_timer->start();
        // First time the feature is enabled: write a backup right away instead
        // of waiting a day for the first one.
        if (m_settings->lastBackupMs() == 0)
            runNow();
    } else {
        m_timer->stop();
    }
}

void BackupService::maybeRunCatchUp()
{
    if (!m_settings->backupsEnabled())
        return;
    const qint64 last = m_settings->lastBackupMs();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (last == 0 || now - last >= kBackupIntervalMs)
        runNow();
}
