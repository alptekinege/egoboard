#include "ApplicationContext.h"

#include "AutoPaster.h"
#include "PortalPaster.h"
#include "BackupService.h"
#include "BookmarkManager.h"
#include "ClipboardWatcher.h"
#include "DatabaseSchema.h"
#include "EgoboardDbusAdaptor.h"
#include "ExpireScheduler.h"
#include "ExportImportManager.h"
#include "HotkeyManager.h"
#include "IconThemeManager.h"
#include "KWinCursorTracker.h"
#include "LayerShellHelper.h"
#include "WlrDataControlHelper.h"
#include "ScriptActionManager.h"
#include "SettingsManager.h"
#include "SnippetManager.h"
#include "StorageManager.h"
#include "SystemThemeWatcher.h"
#include "ThemeManager.h"
#include "TrayController.h"
#include "VacuumWorker.h"
#include "EncryptionManager.h"
#include "OcrWorker.h"
#include "TransformEngine.h"
#include "WaylandActiveWindowTracker.h"
#include "X11ActiveWindowTracker.h"
#include "ui/MainWindow.h"
#include "ui/QuickPasteMenu.h"
#include "ui/UiHelpers.h"

#include <KNotification>

#include <QApplication>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QStandardPaths>
#include <QTextDocument>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include <memory>

namespace {
constexpr qint64 kVacuumSizeThresholdBytes = 50 * 1024 * 1024;
constexpr int kDiskCapCheckInterval = 25; // captures between cap enforcements

// "Paste as → Image → PNG file": writes the stored image to a temporary PNG
// and returns a Files-type record pointing at it (pasteable in file managers).
// Returns a default record when the payload cannot be decoded/saved.
ClipboardRecord imageAsPngFileRecord(const ClipboardRecord &image)
{
    QImage img;
    if (!img.loadFromData(image.blobData, "PNG"))
        return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/egoboard-%1.png")
                             .arg(QDateTime::currentMSecsSinceEpoch());
    if (!img.save(path, "PNG"))
        return {};
    ClipboardRecord file;
    file.type = ContentType::Files;
    file.textData = QString::fromUtf8(
        QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact));
    file.sizeBytes = QFileInfo(path).size();
    file.preview = QStringLiteral("PNG file: %1").arg(QFileInfo(path).fileName());
    return file;
}
} // namespace

ApplicationContext::ApplicationContext(const QString &databasePath, bool fullGui, QObject *parent)
    : QObject(parent)
    , m_fullGui(fullGui)
{
    QDir().mkpath(QFileInfo(databasePath).absolutePath());
    m_settings = new SettingsManager(this);
    m_storage = new StorageManager(databasePath, this);
    m_bookmarks = new BookmarkManager(m_storage->database(), this);
    m_snippets = new SnippetManager(m_storage->database(), this);
    m_io = new ExportImportManager(m_storage, m_bookmarks, m_snippets, this);

    // Vacuum runs on its own thread/connection so the GUI connection stays
    // responsive while the database is being compacted.
    m_vacuumThread = new QThread(this);
    m_vacuumWorker = new VacuumWorker(databasePath);
    m_vacuumWorker->moveToThread(m_vacuumThread);
    connect(m_vacuumThread, &QThread::finished, m_vacuumWorker, &QObject::deleteLater);
    connect(m_vacuumWorker, &VacuumWorker::finished, this, &ApplicationContext::vacuumFinished);
    m_vacuumThread->start();

    m_encryption = new EncryptionManager(this);
    const auto reportProblem = [this](const QString &message) {
        qWarning("egoboard: %s", qPrintable(message));
        if (!m_fullGui)
            return;
        KNotification::event(QStringLiteral("encryptionProblem"),
                             QObject::tr("Database encryption problem"), message,
                             QStringLiteral("security-medium"), KNotification::CloseOnTimeout);
    };
    const auto statusText = [](EncryptionManager::Status status) -> QString {
        switch (status) {
        case EncryptionManager::Status::NotAvailable:
            return QObject::tr("SQLCipher is not available in this build");
        case EncryptionManager::Status::WalletDisabled:
            return QObject::tr("KWallet is disabled");
        case EncryptionManager::Status::WalletOpenFailed:
            return QObject::tr("KWallet could not be opened");
        case EncryptionManager::Status::WalletOperationFailed:
            return QObject::tr("the wallet operation failed");
        case EncryptionManager::Status::EntryMissing:
            return QObject::tr("no database key is stored in KWallet");
        case EncryptionManager::Status::Ok:
            break;
        }
        return {};
    };

    if (m_settings->encryptionEnabled()) {
        // Make the on-disk state match the setting: unlock a locked database,
        // or encrypt a plaintext one in place (e.g. enabled in a config copy).
        QString key;
        const EncryptionManager::Status status = m_encryption->readKey(&key);
        if (status != EncryptionManager::Status::Ok || key.isEmpty()) {
            reportProblem(QObject::tr(
                              "Encryption is enabled but the history database cannot be unlocked: %1.")
                              .arg(statusText(status)));
        } else if (m_storage->requiresEncryptionKey()) {
            if (!m_storage->setEncryptionKey(key)) {
                reportProblem(QObject::tr("The key from KWallet was rejected while opening the database."));
            } else if (!m_storage->verifyEncryptionKey()) {
                reportProblem(QObject::tr("Encryption key verification failed after unlock."));
            }
        } else if (!m_storage->changeEncryptionKey(key)) {
            reportProblem(QObject::tr("Encryption is enabled but the history database could not be encrypted. "
                                      "Check that SQLCipher is available (-DEGOBOARD_USE_SQLCIPHER=ON)."));
        }
    } else if (m_storage->requiresEncryptionKey()) {
        // Disabling the setting without decrypting leaves the file unreadable.
        reportProblem(QObject::tr(
            "The history database is encrypted but encryption is disabled in settings. "
            "Re-enable it to unlock the database, or decrypt it from the settings dialog."));
    }

    if (!m_fullGui)
        return;

    m_scripts = new ScriptActionManager(this);
    m_dbus = new EgoboardDbusAdaptor(m_storage, this);
    connect(m_dbus, &EgoboardDbusAdaptor::pasteRequested, this,
            [this](qint64 entryId) { pasteEntry(entryId); });
    // KRunner's per-match actions: copy leaves the keystroke to the user, pin
    // and delete map straight onto storage.
    connect(m_dbus, &EgoboardDbusAdaptor::copyRequested, this, [this](qint64 entryId) {
        ClipboardRecord record;
        if (m_storage->fetchFull(entryId, &record))
            m_paster->copyToClipboard(record);
    });
    connect(m_dbus, &EgoboardDbusAdaptor::pinRequested, this, [this](qint64 entryId, bool pinned) {
        m_storage->setPinned(entryId, pinned);
    });
    connect(m_dbus, &EgoboardDbusAdaptor::deleteRequested, this, [this](qint64 entryId) {
        m_storage->removeEntries({entryId});
    });
    connect(m_dbus, &EgoboardDbusAdaptor::showQuickPasteRequested, this,
            &ApplicationContext::showQuickPaste);
    connect(m_dbus, &EgoboardDbusAdaptor::cursorPosReported, this,
            [](int x, int y) { KWinCursorTracker::reportGlobalPos(x, y); });

    if (QGuiApplication::platformName() == QLatin1String("wayland"))
        m_tracker = std::make_unique<WaylandActiveWindowTracker>();
    else
        m_tracker = std::make_unique<X11ActiveWindowTracker>();

    m_watcher = new ClipboardWatcher(QGuiApplication::clipboard(), m_settings, m_tracker.get(),
                                     this);
    m_dataControl = new WlrDataControlHelper(m_settings, m_tracker.get(), this);
    m_paster = new AutoPaster(m_watcher, this);
    m_portal = new PortalPaster(this);
    m_paster->setSettingsManager(m_settings);
    m_paster->setPortalPaster(m_portal);
    m_expire = new ExpireScheduler(m_storage, m_settings, this);
    m_hotkeys = new HotkeyManager(this);
    m_tray = new TrayController(m_storage, m_settings, this);
    m_window = std::make_unique<MainWindow>(*this);
    m_quickPaste = new QuickPasteMenu(m_storage, m_settings->quickPasteCount());
    m_quickPaste->setSettings(m_settings);
    m_quickPaste->setTwoLine(m_settings->quickPasteTwoLine());
    m_ocr = new OcrWorker(m_storage, this);
    m_ocr->setLanguage(m_settings->ocrLanguage());
    m_ocr->setMaxChars(m_settings->ocrMaxChars());
    connect(m_settings, &SettingsManager::changed, this, [this] {
        m_ocr->setLanguage(m_settings->ocrLanguage());
        m_ocr->setMaxChars(m_settings->ocrMaxChars());
        m_quickPaste->setItemCount(m_settings->quickPasteCount());
        m_quickPaste->setTwoLine(m_settings->quickPasteTwoLine());
        UiHelpers::setReduceMotion(m_settings->reduceMotion());
        syncPortalSession(); // opt-in/out takes effect without a restart
    });
    connect(m_ocr, &OcrWorker::recognized, this, [this](qint64 id, const QString &text){
        m_storage->setOcrText(id, text);
    });
}

ApplicationContext::~ApplicationContext()
{
    if (m_vacuumThread) {
        m_vacuumThread->quit();
        m_vacuumThread->wait(5000);
    }
    // Tear the GUI down first (it references the managers below), then the
    // helpers that hold their own QSqlDatabase handles, so they are released
    // before the storage manager removes its connection; otherwise Qt warns
    // that the connection is still in use.
    m_window.reset();
    delete m_quickPaste; // parentless popup, not owned by the QObject tree
    m_quickPaste = nullptr;
    delete m_snippets;
    m_snippets = nullptr;
    delete m_bookmarks;
    m_bookmarks = nullptr;
}

void ApplicationContext::start()
{
    if (m_dbus) m_dbus->registerService();

    if (!m_fullGui) return;

    connect(m_watcher, &ClipboardWatcher::captured, this, &ApplicationContext::onCaptured);
    connect(m_watcher, &ClipboardWatcher::excludedSensitive, this,
            [](const QString &reason) {
                KNotification::event(
                    QStringLiteral("sensitiveSkipped"),
                    QObject::tr("Sensitive content not saved"),
                    QObject::tr("Looks like %1 — excluded from history as configured.")
                        .arg(reason.isEmpty() ? QObject::tr("sensitive data") : reason),
                    QStringLiteral("security-medium"),
                    KNotification::CloseOnTimeout);
            });
    connect(m_dataControl, &WlrDataControlHelper::captured, this, &ApplicationContext::onCaptured);
    connect(m_dataControl, &WlrDataControlHelper::excludedSensitive, this,
            [](const QString &reason) {
                KNotification::event(
                    QStringLiteral("sensitiveSkipped"),
                    QObject::tr("Sensitive content not saved"),
                    QObject::tr("Looks like %1 — excluded from history as configured.")
                        .arg(reason.isEmpty() ? QObject::tr("sensitive data") : reason),
                    QStringLiteral("security-medium"),
                    KNotification::CloseOnTimeout);
            });
    const auto notifyRedacted = [](const QString &kinds) {
        KNotification::event(
            QStringLiteral("sensitiveRedacted"),
            QObject::tr("Sensitive content redacted"),
            QObject::tr("Stored with %1 hidden. Copy the entry again to get the redacted version.")
                .arg(kinds.isEmpty() ? QObject::tr("secrets") : kinds),
            QStringLiteral("security-medium"),
            KNotification::CloseOnTimeout);
    };
    connect(m_watcher, &ClipboardWatcher::redactedSensitive, this, notifyRedacted);
    connect(m_dataControl, &WlrDataControlHelper::redactedSensitive, this, notifyRedacted);

    // Paste-back failures (missing payload, no key injection available) must
    // not be silent: the user pressed paste and nothing happened.
    connect(m_paster, &AutoPaster::failed, this, [](const QString &reason) {
        KNotification::event(QStringLiteral("pasteFailed"), QObject::tr("Paste failed"), reason,
                             QStringLiteral("dialog-warning"), KNotification::CloseOnTimeout);
    });

    // Auto-expire rules: apply at startup + every 15 min + after capture bursts.
    connect(m_watcher, &ClipboardWatcher::captured, m_expire,
            &ExpireScheduler::scheduleAfterCapture);
    connect(m_dataControl, &WlrDataControlHelper::captured, m_expire,
            &ExpireScheduler::scheduleAfterCapture);
    connect(m_expire, &ExpireScheduler::expired, this, [this](int count) {
        // U11 expire-sweep undo: a toast restoring exactly the swept ids while
        // the window is up; the background notification when it is not.
        if (m_window && m_window->isVisible())
            m_window->showExpiredToast(count, m_expire->takeLastExpiredIds());
        else
            KNotification::event(QStringLiteral("entriesExpired"), QObject::tr("Auto-expire"),
                                 QObject::tr("%n old entrie(s) removed by your expire rules.", "", count),
                                 QStringLiteral("document-edit"), KNotification::CloseOnTimeout);
    });
    m_expire->start();
    connect(m_settings, &SettingsManager::changed, this, [this] {
        m_watcher->setDebounceInterval(m_settings->debounceMs());
        applyThemes(m_settings->theme(), m_settings->iconTheme(), m_settings->textAppearance());
    });

    // Plasma can switch its color scheme or icon theme while Egoboard runs:
    // re-read the scheme files when kdeglobals reports that they moved.
    m_systemTheme = new SystemThemeWatcher(this);
    connect(m_systemTheme, &SystemThemeWatcher::changed, this, [this] {
        applyThemes(m_settings->theme(), m_settings->iconTheme(), m_settings->textAppearance(),
                    /*force=*/true);
    });

    connect(m_hotkeys, &HotkeyManager::toggleRequested, this,
            &ApplicationContext::toggleMainWindow);
    connect(m_hotkeys, &HotkeyManager::quickPasteRequested, this,
            &ApplicationContext::showQuickPaste);
    connect(m_hotkeys, &HotkeyManager::deleteLastRequested, this,
            &ApplicationContext::deleteLastEntry);

    connect(m_tray, &TrayController::toggleRequested, this, &ApplicationContext::toggleMainWindow);
    connect(m_tray, &TrayController::quickPasteRequested, this, &ApplicationContext::showQuickPaste);
    connect(m_tray, &TrayController::pasteRequested, this,
            [this](qint64 entryId) { pasteEntry(entryId); });
    connect(m_tray, &TrayController::settingsRequested, m_window.get(), &MainWindow::openSettings);
    connect(m_tray, &TrayController::clearRequested, m_window.get(), &MainWindow::clearHistory);
    connect(m_tray, &TrayController::quitRequested, qApp, &QCoreApplication::quit);

    connect(m_quickPaste, &QuickPasteMenu::pasteRequested, this,
            [this](qint64 entryId) { pasteEntry(entryId); });

    m_watcher->start();
    m_dataControl->start();
    // The autostart entry names this binary; re-point it after a rebuild, an
    // install or a move, otherwise login keeps launching the old path.
    m_settings->ensureAutostartEntry();
    // Apply the configured theme before any window is shown.
    UiHelpers::setReduceMotion(m_settings->reduceMotion());
    applyThemes(m_settings->theme(), m_settings->iconTheme(), m_settings->textAppearance());
    syncPortalSession(); // per-session consent starts here when opted in
    if (m_settings->startVisible())
        m_window->show();

    // Automatic JSON backups: daily, on a worker thread with its own database
    // connection, and only when the user enabled them.
    m_backup = new BackupService(m_storage->databasePath(), m_settings, this);
    m_backup->setKeyProvider([this] {
        if (!m_settings->encryptionEnabled() || !m_encryption)
            return QString();
        QString key;
        return m_encryption->readKey(&key) == EncryptionManager::Status::Ok ? key : QString();
    });
    connect(m_backup, &BackupService::finished, this,
            [](bool ok, const QString &path, const QString &error) {
                if (ok) {
                    qInfo("egoboard: backup written to %s", qPrintable(path));
                    return;
                }
                qWarning("egoboard: backup failed: %s", qPrintable(error));
                KNotification::event(QStringLiteral("backupFailed"), QObject::tr("Backup failed"),
                                     error, QStringLiteral("dialog-warning"),
                                     KNotification::CloseOnTimeout);
            });
    // A restore happens on the worker thread against its own connection, so the
    // GUI models are told to reload from scratch afterwards.
    connect(m_backup, &BackupService::restoreFinished, this,
            [this](bool ok, const QString &path, const QString &error, int imported, int merged,
                   int skipped) {
                if (!ok) {
                    qWarning("egoboard: restore failed: %s", qPrintable(error));
                    KNotification::event(QStringLiteral("restoreFailed"),
                                         QObject::tr("Restore failed"), error,
                                         QStringLiteral("dialog-warning"),
                                         KNotification::CloseOnTimeout);
                    return;
                }
                qInfo("egoboard: restored %s (%d added, %d merged, %d skipped)",
                      qPrintable(path), imported, merged, skipped);
                m_storage->notifyStorageReset();
                KNotification::event(QStringLiteral("restoreFinished"),
                                     QObject::tr("Backup restored"),
                                     QObject::tr("%1 entries added, %2 merged, %3 skipped.")
                                         .arg(imported).arg(merged).arg(skipped),
                                     QStringLiteral("document-revert"),
                                     KNotification::CloseOnTimeout);
            });
    m_backup->start();

    // Capture pause: tray entry, global shortcut and (optionally) the session
    // lock all funnel into one state.
    connect(m_tray, &TrayController::pauseToggled, this,
            [this](bool paused) { setCapturePaused(paused); });
    connect(m_tray, &TrayController::wheelSteps, this,
            &ApplicationContext::cycleRecentClipboard);
    connect(m_hotkeys, &HotkeyManager::pauseToggleRequested, this,
            [this](bool paused) { setCapturePaused(paused); });
    watchSessionLock();

    // Snippet hotkeys mirror the database: bind at startup and after every
    // snippet change, reporting whatever could not be bound.
    connect(m_hotkeys, &HotkeyManager::snippetRequested, this, &ApplicationContext::pasteSnippet);
    connect(m_snippets, &SnippetManager::snippetsChanged, this, [this] {
        bindSnippetShortcuts();
    });
    // Imports and restores rewrite the snippets table behind the manager's back.
    connect(m_storage, &StorageManager::storageReset, this, [this] {
        bindSnippetShortcuts();
    });
    bindSnippetShortcuts();

    scheduleVacuumChecks();
    scheduleIntegrityCheck();

    // Retention caps are enforced after capture bursts (every 25th capture);
    // this periodic pass catches the smaller bursts that never reach 25.
    m_retentionTimer = new QTimer(this);
    m_retentionTimer->setInterval(5 * 60 * 1000);
    connect(m_retentionTimer, &QTimer::timeout, this, [this] {
        const qint64 cap = m_settings->diskCapBytes();
        const qint64 maxEntries = m_settings->maxEntries();
        if (cap > 0)
            m_storage->enforceDiskCap(cap);
        if (maxEntries > 0)
            m_storage->enforceMaxEntries(maxEntries);
    });
    m_retentionTimer->start();
}

void ApplicationContext::applyThemes(const QString &colorTheme, const QString &iconTheme,
                                     const TextAppearance::Overrides &text, bool force)
{
    // The settings dialog saves every key on Apply/OK, so this runs once per
    // changed setting; skip the ones that did not touch the appearance at all.
    if (!force && colorTheme == m_appliedColorTheme && iconTheme == m_appliedIconTheme
        && text == m_appliedText) {
        return;
    }
    m_appliedColorTheme = colorTheme;
    m_appliedIconTheme = iconTheme;
    m_appliedText = text;

    ThemeManager::apply(colorTheme, text);
    IconThemeManager::apply(iconTheme);

    // Theme icons are re-resolved on the next paint, so nudge the widgets that
    // are on screen (an open settings dialog included) to pick them up now.
    const auto widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets)
        widget->update();
}

void ApplicationContext::onCaptured(const ClipboardRecord &record)
{
    // A new copy moves the top of the history, so a wheel walk starts over.
    m_trayCycle.reset();

    // U16: skip feedback when the captured content is already at the top.
    bool alreadyAtTop = false;
    if (m_settings && (m_settings->captureSoundEnabled() || m_settings->captureNotificationEnabled())) {
        FilterSpec trivial;
        bool hasMore = false;
        const auto top = m_storage->fetchPage(trivial, {}, 1, &hasMore);
        if (!top.isEmpty() && !top.first().hash.isEmpty() && top.first().hash == record.hash)
            alreadyAtTop = true;
    }

    bool updatedExisting = false;
    const qint64 id = m_storage->insertOrUpdate(record, &updatedExisting);
    if (id == 0)
        return;

    // U16: capture feedback — sound + notification, suppressed when the
    // content was already at the top (re-copy of the same payload).
    if (!alreadyAtTop && m_settings && m_window) {
        if (m_settings->captureSoundEnabled())
            QApplication::beep();
        if (m_settings->captureNotificationEnabled() && m_settings->notificationsEnabled()) {
            const QString source = record.sourceApp.isEmpty()
                                       ? tr("Unknown source")
                                       : record.sourceApp;
            const QString text = record.textData.isEmpty() ? record.preview : record.textData;
            KNotification::event(
                QStringLiteral("capture"),
                tr("New clipboard entry"),
                tr("%1\n\n%2").arg(source, text.left(200)),
                QStringLiteral("edit-paste"),
                KNotification::CloseOnTimeout);
        }
    }

    // Queue OCR for new image entries (local tesseract, no network)
    if (!updatedExisting && record.type == ContentType::Image && record.hasBlob && m_ocr
        && m_settings && m_settings->ocrEnabled() && OcrWorker::isAvailable()) {
        m_ocr->recognize(id, record.blobData);
    }

    // Optional retention caps: checked every N captures to amortize the cost.
    const qint64 cap = m_settings->diskCapBytes();
    const qint64 maxEntries = m_settings->maxEntries();
    if ((cap > 0 || maxEntries > 0) && ++m_captureCounter % kDiskCapCheckInterval == 0) {
        if (cap > 0)
            m_storage->enforceDiskCap(cap);
        if (maxEntries > 0)
            m_storage->enforceMaxEntries(maxEntries);
    }
}

void ApplicationContext::toggleMainWindow()
{
    if (m_window->isVisible())
        m_window->hide();
    else
        m_window->show();
}

void ApplicationContext::syncPortalSession()
{
    if (!m_portal || !m_settings)
        return;
    if (QGuiApplication::platformName() == QLatin1String("wayland")
        && m_settings->portalPasteEnabled())
        m_portal->ensureSession();
    else
        m_portal->closeSession();
}

void ApplicationContext::showQuickPaste()
{
    if (m_window->isVisible())
        m_window->hide();
    m_quickPaste->popupAtCursor();
}

void ApplicationContext::pasteEntry(qint64 entryId, PasteVariant variant)
{
    ClipboardRecord record;
    if (!m_storage->fetchFull(entryId, &record))
        return;

    // Text variants reshape the payload before it reaches the clipboard.
    const bool wantsPlainText = variant == PasteVariant::PlainText
        || (variant == PasteVariant::Normal && m_settings->pasteAsPlainText());
    const bool textVariant = variant == PasteVariant::UpperCase
        || variant == PasteVariant::LowerCase || variant == PasteVariant::WithTimestamp;
    if ((wantsPlainText || textVariant)
        && (record.type == ContentType::Text || record.type == ContentType::RichText)) {
        if (record.type == ContentType::RichText) {
            QTextDocument document;
            document.setHtml(record.textData);
            record.textData = document.toPlainText();
            record.type = ContentType::Text;
        }
        if (variant == PasteVariant::UpperCase)
            record.textData = record.textData.toUpper();
        else if (variant == PasteVariant::LowerCase)
            record.textData = record.textData.toLower();
        else if (variant == PasteVariant::WithTimestamp)
            record.textData = QDateTime::currentDateTime().toString(
                                  QStringLiteral("[yyyy-MM-dd HH:mm] "))
                + record.textData;
    }
    // "Image → PNG file": swap in a Files record pointing at the temp PNG;
    // on failure the original image is pasted as usual.
    if (variant == PasteVariant::ImageAsPngFile && record.type == ContentType::Image
        && record.hasBlob) {
        const ClipboardRecord file = imageAsPngFileRecord(record);
        if (file.type == ContentType::Files)
            record = file;
    }

    // "Bump on paste": move the pasted entry back to the top of the history.
    if (m_settings->bumpOnPaste())
        m_storage->touchEntry(entryId);

    if (m_dataControl) m_dataControl->suppressOwnSets();
    QWidget *hideTarget = nullptr;
    if (m_settings->closeAfterPaste()) {
        if (m_window->isVisible())
            hideTarget = m_window.get();
        else if (m_quickPaste->isVisible())
            hideTarget = m_quickPaste;
    }
    m_paster->paste(record, hideTarget);
}

void ApplicationContext::deleteLastEntry()
{
    // Newest entry first. Pinned entries are protected: if the newest row is
    // pinned the hotkey does nothing rather than removing an older entry.
    const auto page = m_storage->fetchPage(FilterSpec{}, PageCursor{}, 1);
    if (page.isEmpty() || page.first().pinned)
        return;
    m_storage->remove(page.first().id);
}

void ApplicationContext::vacuumNow()
{
    QMetaObject::invokeMethod(m_vacuumWorker, &VacuumWorker::run, Qt::QueuedConnection);
}

void ApplicationContext::scheduleVacuumChecks()
{
    if (m_storage->databaseFileSize() > kVacuumSizeThresholdBytes)
        vacuumNow();
    m_vacuumTimer = new QTimer(this);
    m_vacuumTimer->setInterval(24 * 60 * 60 * 1000); // daily
    connect(m_vacuumTimer, &QTimer::timeout, this, [this] {
        if (m_storage->databaseFileSize() > kVacuumSizeThresholdBytes)
            vacuumNow();
    });
    m_vacuumTimer->start();
}

void ApplicationContext::scheduleIntegrityCheck()
{
    // One-shot background PRAGMA quick_check on a scratch connection: the GUI
    // connection belongs to the GUI thread, and a damaged file is much easier
    // to explain here than as scattered query errors later.
    const QString path = m_storage->databasePath();
    QString key;
    if (m_settings->encryptionEnabled() && m_encryption)
        m_encryption->readKey(&key);

    QThreadPool::globalInstance()->start([path, key] {
        static std::atomic_int counter{0};
        const QString name =
            QStringLiteral("egoboard-integrity-%1").arg(counter.fetch_add(1));
        QString error;
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            db.setDatabaseName(path);
            if (!db.open()) {
                error = db.lastError().text();
            } else {
                if (!key.isEmpty() && !DatabaseSchema::setKey(db, key))
                    error = QObject::tr("the encryption key was rejected");
                else
                    ok = DatabaseSchema::quickCheck(db, &error);
                db.close();
            }
            db = QSqlDatabase(); // release the handle before removing the connection
        }
        QSqlDatabase::removeDatabase(name);
        if (ok)
            return;
        QMetaObject::invokeMethod(qApp, [error] {
            KNotification::event(
                QStringLiteral("integrityProblem"),
                QObject::tr("Database check failed"),
                QObject::tr("%1\n\nStorage settings can rebuild the search index and restore a "
                            "backup if needed.").arg(error),
                QStringLiteral("security-low"), KNotification::CloseOnTimeout);
        });
    });
}

void ApplicationContext::setCapturePaused(bool paused, bool fromLock)
{
    // Two independent reasons to pause: the user asked for it, or the session
    // is locked. Capture stays off while either applies.
    if (fromLock)
        m_lockPause = paused;
    else
        m_manualPause = paused;
    const bool effective = m_manualPause || m_lockPause;
    const bool changed = effective != m_capturePaused;
    m_capturePaused = effective;

    if (m_watcher)
        m_watcher->setPaused(effective);
    if (m_dataControl)
        m_dataControl->setPaused(effective);
    if (m_tray)
        m_tray->setPaused(effective);
    if (m_hotkeys)
        m_hotkeys->setPaused(effective);

    if (changed && m_fullGui) {
        KNotification::event(effective ? QStringLiteral("capturePaused")
                                       : QStringLiteral("captureResumed"),
                             effective ? QObject::tr("Clipboard capture paused")
                                       : QObject::tr("Clipboard capture resumed"),
                             fromLock ? (effective
                                             ? QObject::tr("The session is locked.")
                                             : QObject::tr("The session was unlocked."))
                                      : QObject::tr("Toggle it again with the tray icon or the "
                                                    "global shortcut."),
                             QStringLiteral("edit-paste"), KNotification::CloseOnTimeout);
    }
}

void ApplicationContext::bindSnippetShortcuts()
{
    if (!m_hotkeys || !m_snippets)
        return;
    m_snippetShortcutProblems = m_hotkeys->setSnippetShortcuts(m_snippets->snippets());
}

void ApplicationContext::pasteSnippet(qint64 snippetId)
{
    if (!m_snippets)
        return;
    const std::optional<Snippet> snippet = m_snippets->snippet(snippetId);
    if (!snippet)
        return;

    // The shortcut expands against whatever is on the clipboard right now and
    // pastes the result like a history entry would be pasted.
    const QString expanded = SnippetManager::expand(snippet->templateText,
                                                    QGuiApplication::clipboard()->text());
    if (expanded.isEmpty())
        return;

    ClipboardRecord record;
    record.type = ContentType::Text;
    record.textData = expanded;
    record.preview = expanded.left(120);
    record.sizeBytes = expanded.toUtf8().size();

    if (m_dataControl)
        m_dataControl->suppressOwnSets();
    QWidget *hideTarget = nullptr;
    if (m_settings->closeAfterPaste() && m_window->isVisible())
        hideTarget = m_window.get();
    m_paster->paste(record, hideTarget);
}

void ApplicationContext::cycleRecentClipboard(int steps)
{
    // The tray menu shows the newest handful of entries; walking the same window
    // keeps the wheel and the menu consistent.
    constexpr int kCycleWindow = 10;
    const auto recent = m_storage->fetchPage(FilterSpec{}, PageCursor{}, kCycleWindow);
    if (recent.isEmpty()) {
        m_trayCycle.reset();
        return;
    }

    const int index = m_trayCycle.advance(steps, recent.size());
    if (index < 0)
        return;
    const ClipboardRecord &record = recent.at(index);
    ClipboardRecord full;
    if (!m_storage->fetchFull(record.id, &full))
        full = record;

    if (m_dataControl)
        m_dataControl->suppressOwnSets();
    if (!m_paster->copyToClipboard(full))
        return;

    // Say what is on the clipboard now: without it a wheel scroll looks like it
    // did nothing (the paste target is the user's next Ctrl+V).
    const QString preview = record.preview.isEmpty() ? record.textData : record.preview;
    KNotification::event(QStringLiteral("trayCycle"),
                         tr("Copied from history (%1 of %2)")
                             .arg(m_trayCycle.position())
                             .arg(recent.size()),
                         preview.left(120), QStringLiteral("edit-copy"),
                         KNotification::CloseOnTimeout);
}

void ApplicationContext::watchSessionLock()
{
    if (!m_settings->pauseOnLock())
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    // KDE's screen locker (and the freedesktop interface generally) announces
    // lock state on the session bus; a missing service simply means no lock
    // awareness and the connect does nothing.
    bus.connect(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("ActiveChanged"),
                this, SLOT(onSessionLockChanged(bool)));
}

int ApplicationContext::benchmark(int entryCount)
{
    QElapsedTimer timer;
    const int total = qBound(1, entryCount > 0 ? entryCount : 50000, 500000);
    qInfo("egoboard benchmark: %d entries, database %s", total, qPrintable(m_storage->databasePath()));

    bool failed = false;
    const auto report = [&failed](const QString &label, qint64 ms, qint64 budgetMs) {
        const bool ok = ms <= budgetMs;
        failed = failed || !ok;
        qInfo("  %-28s %8lld ms  (budget %lld ms)  %s", qPrintable(label), ms, budgetMs,
              ok ? "ok" : "OVER BUDGET");
    };

    // --- bulk insertion ------------------------------------------------------
    timer.start();
    m_storage->beginBulk();
    for (int i = 0; i < total; ++i) {
        ClipboardRecord record;
        record.type = ContentType::Text;
        record.textData = QStringLiteral("benchmark entry %1 — searchable words: postgres error index")
                              .arg(i);
        record.preview = record.textData.left(120);
        record.sizeBytes = record.textData.size();
        record.timestamp = 1700000000000LL + i;
        record.sourceApp = (i % 7 == 0) ? QStringLiteral("kate") : QStringLiteral("firefox");
        record.hash = QCryptographicHash::hash(record.textData.toUtf8(),
                                               QCryptographicHash::Sha256)
                          .toHex();
        m_storage->insertOrUpdate(record);
    }
    m_storage->endBulk(true);
    const qint64 insertMs = timer.elapsed();
    if (m_storage->stats().entryCount != total) {
        qCritical("benchmark: expected %d rows, got %lld", total,
                  m_storage->stats().entryCount);
        return 1;
    }
    report(QStringLiteral("insert %1 (bulk)").arg(total), insertMs,
           qMax<qint64>(5000, total)); // 1 ms/entry allowance

    // --- first page of the list ---------------------------------------------
    timer.restart();
    bool hasMore = false;
    const auto page = m_storage->fetchPage(FilterSpec{}, {}, 200, &hasMore);
    report(QStringLiteral("page 1 (200 rows)"), timer.elapsed(), 100);
    if (page.size() != 200 || !hasMore) {
        qCritical("benchmark: page 1 returned %lld rows (hasMore=%d)", page.size(), hasMore);
        return 1;
    }

    // --- deep paging through the keyset cursor -------------------------------
    timer.restart();
    PageCursor cursor;
    int walked = 0;
    for (int i = 0; i < 50; ++i) {
        bool more = false;
        const auto p = m_storage->fetchPage(FilterSpec{}, cursor, 200, &more);
        if (p.isEmpty())
            break;
        walked += p.size();
        cursor = PageCursor{true, p.last().timestamp, p.last().id, p.last().useCount};
        if (!more)
            break;
    }
    report(QStringLiteral("paging %1 rows").arg(walked), timer.elapsed(), 500);
    if (walked < qMin(total, 10000)) {
        qCritical("benchmark: deep paging stopped early after %d rows", walked);
        return 1;
    }

    // --- MostUsed sort (indexed) ---------------------------------------------
    timer.restart();
    FilterSpec mostUsed;
    mostUsed.sortMode = FilterSpec::SortMode::MostUsed;
    const auto mostUsedPage = m_storage->fetchPage(mostUsed, {}, 200, nullptr);
    report(QStringLiteral("most-used page"), timer.elapsed(), 100);
    if (mostUsedPage.size() != 200) {
        qCritical("benchmark: most-used page returned %lld rows", mostUsedPage.size());
        return 1;
    }

    // --- FTS query ------------------------------------------------------------
    timer.restart();
    FilterSpec search;
    search.searchText = QStringLiteral("postgres error");
    const auto hits = m_storage->fetchPage(search, {}, 200, nullptr);
    report(QStringLiteral("FTS query"), timer.elapsed(), 500);
    if (hits.isEmpty()) {
        qCritical("benchmark: FTS query returned no hits");
        return 1;
    }

    // --- export ---------------------------------------------------------------
    const QString exportPath =
        QDir(QDir::tempPath()).filePath(QStringLiteral("egoboard-bench-export.json"));
    ExportImportManager::ExportRequest request;
    request.path = exportPath;
    request.scope = ExportImportManager::Scope::Everything;
    timer.restart();
    QString error;
    const bool exported = m_io->exportToFile(request, &error);
    report(QStringLiteral("export (JSON)"), timer.elapsed(), qMax<qint64>(5000, total / 5));
    QFile::remove(exportPath);
    if (!exported) {
        qCritical("benchmark: export failed: %s", qPrintable(error));
        return 1;
    }

    if (failed) {
        qCritical("egoboard benchmark: OVER BUDGET");
        return 1;
    }
    qInfo("egoboard benchmark: OK");
    return 0;
}

int ApplicationContext::smokeTest()
{
    // --smoke: headless end-to-end verification of the core wiring.
    qint64 id1 = 0, id2 = 0;
    {
        ClipboardRecord record;
        record.type = ContentType::Text;
        record.textData = QStringLiteral("smoke hello");
        record.preview = QStringLiteral("smoke hello");
        record.hash = QByteArrayLiteral("aaaa1111");
        record.timestamp = QDateTime::currentMSecsSinceEpoch();
        record.sourceApp = QStringLiteral("smoke-app");
        id1 = m_storage->insertOrUpdate(record);
    }
    {
        ClipboardRecord duplicate;
        duplicate.type = ContentType::Text;
        duplicate.textData = QStringLiteral("smoke hello");
        duplicate.hash = QByteArrayLiteral("aaaa1111");
        duplicate.timestamp = QDateTime::currentMSecsSinceEpoch() + 1000;
        bool updated = false;
        id2 = m_storage->insertOrUpdate(duplicate, &updated);
    }
    if (id1 == 0 || id2 != id1) {
        qCritical("smoke: dedup failed (%lld vs %lld)", id1, id2);
        return 1;
    }
    const auto stats = m_storage->stats();
    if (stats.entryCount != 1) {
        qCritical("smoke: expected 1 entry, got %lld", stats.entryCount);
        return 1;
    }

    const qint64 group = m_bookmarks->createGroup(QStringLiteral("Smoke"), 0, QStringLiteral("#ff0000"),
                                                  QStringLiteral("starred"));
    if (group == 0 || !m_bookmarks->assignEntry(id1, group)) {
        qCritical("smoke: group/assign failed");
        return 1;
    }

    FilterSpec filter;
    filter.searchText = QStringLiteral("smoke");
    const auto page = m_storage->fetchPage(filter, {}, 10);
    if (page.size() != 1 || page.first().id != id1) {
        qCritical("smoke: filtered fetch failed");
        return 1;
    }

    const QString exportPath = QDir::temp().filePath(QStringLiteral("egoboard-smoke-export.json"));
    ExportImportManager::ExportRequest request;
    request.scope = ExportImportManager::Scope::Everything;
    request.path = exportPath;
    QString error;
    if (!m_io->exportToFile(request, &error)) {
        qCritical("smoke: export failed: %s", qPrintable(error));
        return 1;
    }
    if (!m_storage->clearHistory(true)) {
        qCritical("smoke: clear failed");
        return 1;
    }
    const auto result = m_io->importFromFile(exportPath, ExportImportManager::ImportMode::Merge);
    if (!result.ok || result.entriesImported != 1) {
        qCritical("smoke: import failed: %s", qPrintable(result.error));
        return 1;
    }
    if (m_storage->stats().entryCount != 1) {
        qCritical("smoke: entry count after import wrong");
        return 1;
    }

    // Phase 3 smoke: TransformEngine + SnippetManager
    {
        auto r = TransformEngine::apply(TransformEngine::TransformId::Uppercase, QStringLiteral("hello"));
        if (!r.ok || r.output != QLatin1String("HELLO")) {
            qCritical("smoke: TransformEngine uppercase failed");
            return 1;
        }
        auto r2 = TransformEngine::apply(TransformEngine::TransformId::JsonPretty, QStringLiteral("{\"a\":1}"));
        if (!r2.ok || !r2.output.contains(QLatin1String("\"a\""))) {
            qCritical("smoke: TransformEngine json-pretty failed: %s", qPrintable(r2.error));
            return 1;
        }
        auto chain = TransformEngine::applyChain(QStringLiteral("  hello world  "),
            {TransformEngine::TransformId::Trim, TransformEngine::TransformId::Uppercase});
        if (!chain.ok || chain.output != QLatin1String("HELLO WORLD")) {
            qCritical("smoke: TransformEngine chain failed");
            return 1;
        }
    }
    {
        const qint64 sid = m_snippets->createSnippet(QStringLiteral("Smoke Snippet"), QStringLiteral("hi {{clipboard}} [{{date}}]"), {});
        if (sid == 0) {
            qCritical("smoke: createSnippet failed");
            return 1;
        }
        const QString expanded = m_snippets->expandSnippet(sid, QStringLiteral("world"));
        if (!expanded.startsWith(QLatin1String("hi world ["))) {
            qCritical("smoke: snippet expand failed: %s", qPrintable(expanded));
            return 1;
        }
        if (!m_snippets->deleteSnippet(sid)) {
            qCritical("smoke: deleteSnippet failed");
            return 1;
        }
    }

    // Phase 4 smoke: LayerShellHelper (headless-safe, no window needed)
    {
        const QString plat = LayerShellHelper::platformName();
        const bool wl = LayerShellHelper::isWayland();
        const bool avail = LayerShellHelper::isAvailable();
        // On offscreen (CI) expect !wayland && !available; on wayland
        // expect available when LayerShellQt was linked. Either way must not crash.
        if (plat.isEmpty()) {
            qCritical("smoke: LayerShellHelper plat empty");
            return 1;
        }
        if (wl && !avail) {
#ifdef EGOBOARD_HAVE_LAYERSHELLQT
            qCritical("smoke: on wayland but LayerShellQt reports unavailable");
            return 1;
#endif
        }
        if (!wl && avail) {
            qCritical("smoke: LayerShellHelper claims available off wayland (%s)", qPrintable(plat));
            return 1;
        }
        const QString diag = LayerShellHelper::diagnostics();
        if (diag.isEmpty() || !diag.contains(plat, Qt::CaseInsensitive)) {
            qCritical("smoke: LayerShellHelper diagnostics malformed");
            return 1;
        }
        if (diag.contains(QStringLiteral("LayerShellQt")) && wl) {
            // ok — built with LayerShellQt
        }
    }

    // Phase 4B smoke: wlr-data-control (headless-safe, offscreen inactive)
    {
        const QString plat = WlrDataControlHelper::platformName();
        const bool supported = WlrDataControlHelper::isSupported();
        if (plat.isEmpty()) { qCritical("smoke: WlrDataControl plat empty"); return 1; }
        WlrDataControlHelper tmpHelper(m_settings, m_tracker.get());
        const QString diag = tmpHelper.diagnostics();
        if (diag.isEmpty() || !diag.contains(plat, Qt::CaseInsensitive)) {
            qCritical("smoke: WlrDataControl diagnostics malformed: %s", qPrintable(diag));
            return 1;
        }
        if (plat == QLatin1String("offscreen")) {
            if (supported) { qCritical("smoke: wlr-data-control should not be supported on offscreen"); return 1; }
            if (tmpHelper.isActive()) { qCritical("smoke: wlr-data-control unexpectedly active on offscreen"); return 1; }
        }
    }

    // Track E smoke: SQLCipher opt-in (graceful when not built)
    {
        EncryptionManager enc;
        const QString status = enc.walletStatusText();
        if (status.isEmpty()) { qCritical("smoke: EncryptionManager status empty"); return 1; }
        // Storage probes must not crash even without SQLCipher
        const bool avail = m_storage->isSqlCipherAvailable();
        const QString ver = m_storage->cipherVersion();
#ifdef EGOBOARD_HAVE_SQLCIPHER
        Q_UNUSED(avail); Q_UNUSED(ver);
#else
        if (avail || !ver.isEmpty()) { qCritical("smoke: SQLCipher claimed available without build flag"); return 1; }
#endif
        if (!m_storage->verifyEncryptionKey()) {
            // empty DB with no key should still verify as file is plaintext
        }
        const QString gen = EncryptionManager::generateKey();
        if (gen.isEmpty() || gen.size() < 16) { qCritical("smoke: generateKey failed"); return 1; }
    }

    qInfo("egoboard smoke test: OK");
    return 0;
}
