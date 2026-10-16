#include "BackupService.h"

#include "BackupArchive.h"
#include "BookmarkManager.h"
#include "ExportImportManager.h"
#include "SettingsManager.h"
#include "SnippetManager.h"
#include "StorageManager.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
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

void BackupWorker::requestRun(const QString &folder, int keep, const QByteArray &settingsJson,
                              const QString &encryptionKey)
{
    m_cancel.store(false, std::memory_order_relaxed);
    if (QThread::currentThread() == thread()) {
        run(folder, keep, settingsJson, encryptionKey);
        return;
    }
    QMetaObject::invokeMethod(
        this, [this, folder, keep, settingsJson, encryptionKey] {
            run(folder, keep, settingsJson, encryptionKey);
        },
        Qt::QueuedConnection);
}

void BackupWorker::requestRestore(const QString &path, ExportImportManager::ImportMode mode,
                                  const QString &encryptionKey, bool restoreSettings)
{
    m_cancel.store(false, std::memory_order_relaxed);
    if (QThread::currentThread() == thread()) {
        restore(path, mode, encryptionKey, restoreSettings);
        return;
    }
    QMetaObject::invokeMethod(
        this, [this, path, mode, encryptionKey, restoreSettings] {
            restore(path, mode, encryptionKey, restoreSettings);
        },
        Qt::QueuedConnection);
}

void BackupWorker::requestCancel()
{
    m_cancel.store(true, std::memory_order_relaxed);
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
    if (m_storage->requiresEncryptionKey()) {
        m_storage.reset();
        return false;
    }
    m_bookmarks = std::make_unique<BookmarkManager>(m_storage->database());
    m_snippets = std::make_unique<SnippetManager>(m_storage->database());
    m_io = std::make_unique<ExportImportManager>(m_storage.get(), m_bookmarks.get(),
                                                 m_snippets.get());
    return true;
}

void BackupWorker::run(const QString &folder, int keep, const QByteArray &settingsJson,
                       const QString &encryptionKey)
{
    if (!ensureStorage(encryptionKey)) {
        const bool locked = !encryptionKey.isEmpty();
        emit finished(false, {},
                      locked ? tr("Cannot unlock the encrypted history database.")
                             : tr("Cannot open the history database."));
        return;
    }

    QString path;
    QString error;
    const bool ok = BackupArchive::write(
        m_storage.get(), m_io.get(), folder, keep, settingsJson, encryptionKey, &m_cancel,
        [this](int done, int total) { emit backupProgress(done, total); }, &path, &error);
    emit finished(ok, path, error);
}

void BackupWorker::restore(const QString &path, ExportImportManager::ImportMode mode,
                           const QString &encryptionKey, bool restoreSettings)
{
    if (!ensureStorage(encryptionKey)) {
        const bool locked = !encryptionKey.isEmpty();
        emit restoreFinished(false, path,
                             locked ? tr("Cannot unlock the encrypted history database.")
                                    : tr("Cannot open the history database."),
                             0, 0, 0, {});
        return;
    }

    QString historyPath = path;
    QByteArray settingsJson;
    QTemporaryDir extracted;
    if (BackupArchive::isBundlePath(path)) {
        if (!extracted.isValid()) {
            emit restoreFinished(false, path, tr("Cannot create a private restore workspace."),
                                 0, 0, 0, {});
            return;
        }
        BackupArchive::Package package;
        QString error;
        const bool extractedOk = BackupArchive::extract(
            path, extracted.path(), &m_cancel,
            [this](int done, int total) { emit restoreProgress(done, total); }, &package, &error);
        if (!extractedOk) {
            emit restoreFinished(false, path, error, 0, 0, 0, {});
            return;
        }
        if (package.databaseEncrypted != m_storage->isEncrypted()) {
            emit restoreFinished(
                false, path,
                tr("The backup SQLCipher state does not match the current database; restore it "
                   "after enabling or disabling encryption to match the backup."),
                0, 0, 0, {});
            return;
        }
        if (restoreSettings && mode == ExportImportManager::ImportMode::Overwrite)
            settingsJson = package.settingsJson;
        historyPath = package.payloadPath;
        if (package.payloadIsDatabase) {
            auto archiveStorage = std::make_unique<StorageManager>(package.payloadPath);
            if (!archiveStorage->database().isOpen()) {
                emit restoreFinished(false, path,
                                     tr("The database inside the backup cannot be opened."),
                                     0, 0, 0, {});
                return;
            }
            if (package.databaseEncrypted) {
                if (encryptionKey.isEmpty()
                    || !archiveStorage->setEncryptionKey(encryptionKey)
                    || !archiveStorage->isEncrypted()) {
                    emit restoreFinished(false, path,
                                         tr("The encrypted backup cannot be unlocked with the KWallet key."),
                                         0, 0, 0, {});
                    return;
                }
            } else if (archiveStorage->requiresEncryptionKey()) {
                emit restoreFinished(false, path,
                                     tr("The backup encryption metadata does not match its database."),
                                     0, 0, 0, {});
                return;
            }
            QString checkError;
            if (!archiveStorage->quickCheck(&checkError)) {
                emit restoreFinished(false, path,
                                     tr("The database inside the backup failed its integrity check: %1")
                                         .arg(checkError),
                                     0, 0, 0, {});
                return;
            }
            if (mode == ExportImportManager::ImportMode::Overwrite) {
                const int entryCount = int(archiveStorage->stats().entryCount);
                if (!BackupArchive::restoreDatabase(
                        archiveStorage->database(), m_storage->database(), &m_cancel,
                        [this](int done, int total) { emit restoreProgress(done, total); },
                        &error)) {
                    emit restoreFinished(false, path, error, 0, 0, 0, {});
                    return;
                }
                emit restoreFinished(true, path, {}, entryCount, 0, 0, settingsJson);
                return;
            }
            BookmarkManager archiveBookmarks(archiveStorage->database());
            SnippetManager archiveSnippets(archiveStorage->database());
            ExportImportManager archiveIo(archiveStorage.get(), &archiveBookmarks,
                                          &archiveSnippets);
            ExportImportManager::ExportRequest request;
            request.scope = ExportImportManager::Scope::Everything;
            request.path = extracted.filePath(QStringLiteral("history.json"));
            request.includeSensitive = true;
            if (!archiveIo.exportToFile(
                    request, &error, &m_cancel,
                    [this](int done, int total) { emit restoreProgress(done, total); })) {
                emit restoreFinished(false, path, error, 0, 0, 0, {});
                return;
            }
            QFile::setPermissions(request.path, QFile::ReadOwner | QFile::WriteOwner);
            historyPath = request.path;
        }
    }

    const ExportImportManager::ImportResult result = m_io->importFromFile(
        historyPath, mode, &m_cancel,
        [this](int done, int total) { emit restoreProgress(done, total); });
    emit restoreFinished(result.ok, path, result.error, result.entriesImported,
                         result.entriesMerged, result.entriesSkipped,
                         result.ok ? settingsJson : QByteArray{});
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
        if (m_worker)
            m_worker->requestCancel();
        m_thread->quit();
        // Export/import checks cancellation between bounded pages. Join before
        // QObject tears down the worker and its thread-affine SQL connection.
        m_thread->wait();
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
        connect(m_worker, &BackupWorker::backupProgress, this, &BackupService::backupProgress);
        connect(m_worker, &BackupWorker::restoreProgress, this, &BackupService::restoreProgress);
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
                       int merged, int skipped, const QByteArray &settingsJson) {
                    m_running = false;
                    QString finalError = error;
                    if (ok && !settingsJson.isEmpty()) {
                        QJsonParseError parseError{};
                        const QJsonDocument document = QJsonDocument::fromJson(settingsJson,
                                                                               &parseError);
                        if (parseError.error != QJsonParseError::NoError || !document.isObject()
                            || !m_settings->importFromJson(document.object(), &finalError)) {
                            ok = false;
                            if (finalError.isEmpty())
                                finalError = tr("The backup settings could not be restored.");
                        }
                    }
                    emit restoreFinished(ok, path, finalError, imported, merged, skipped);
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

bool BackupService::zipBackupsAvailable()
{
    return BackupArchive::zipAvailable();
}

bool BackupService::databaseSnapshotAvailable()
{
    return BackupArchive::databaseSnapshotAvailable();
}

bool BackupService::runNow()
{
    // Deliberately allowed while the daily schedule is off: the settings dialog
    // offers a manual "Back up now".
    if (!m_worker || m_running)
        return false;
    m_running = true;
    const QByteArray settingsJson = QJsonDocument(m_settings->exportToJson())
                                        .toJson(QJsonDocument::Compact);
    m_worker->requestRun(folder(), m_settings->backupKeep(), settingsJson, walletKey());
    return true;
}

bool BackupService::restoreNow(const QString &path, ExportImportManager::ImportMode mode,
                               bool restoreSettings)
{
    if (!m_worker || m_running || path.trimmed().isEmpty())
        return false;
    m_running = true;
    m_worker->requestRestore(path.trimmed(), mode, walletKey(), restoreSettings);
    return true;
}

void BackupService::cancel()
{
    if (m_worker)
        m_worker->requestCancel(); // atomic: safe to call from the GUI thread
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
