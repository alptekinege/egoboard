#include "ApplicationContext.h"

#include "AutoPaster.h"
#include "BookmarkManager.h"
#include "ClipboardWatcher.h"
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

#include <KNotification>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTextDocument>
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
    m_io = new ExportImportManager(m_storage, m_bookmarks, this);
    m_snippets = new SnippetManager(m_storage->database(), this);

    // Vacuum runs on its own thread/connection so the GUI connection stays
    // responsive while the database is being compacted.
    m_vacuumThread = new QThread(this);
    m_vacuumWorker = new VacuumWorker(databasePath);
    m_vacuumWorker->moveToThread(m_vacuumThread);
    connect(m_vacuumThread, &QThread::finished, m_vacuumWorker, &QObject::deleteLater);
    m_vacuumThread->start();

    m_encryption = new EncryptionManager(this);
    if (m_settings->encryptionEnabled()) {
        QString key;
        if (m_encryption->readKey(&key) == EncryptionManager::Status::Ok && !key.isEmpty()) {
            if (!m_storage->setEncryptionKey(key)) {
                qWarning("egoboard: encryption key from KWallet failed to unlock database");
            } else if (!m_storage->verifyEncryptionKey()) {
                qWarning("egoboard: encryption enabled but key verification failed");
            }
        }
    }

    if (!m_fullGui)
        return;

    m_scripts = new ScriptActionManager(this);
    m_dbus = new EgoboardDbusAdaptor(m_storage, this);
    connect(m_dbus, &EgoboardDbusAdaptor::pasteRequested, this,
            [this](qint64 entryId) { pasteEntry(entryId); });
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
    m_expire = new ExpireScheduler(m_storage, m_settings, this);
    m_hotkeys = new HotkeyManager(this);
    m_tray = new TrayController(m_storage, this);
    m_window = std::make_unique<MainWindow>(*this);
    m_quickPaste = new QuickPasteMenu(m_storage, m_settings->quickPasteCount());
    m_ocr = new OcrWorker(m_storage, this);
    m_ocr->setLanguage(m_settings->ocrLanguage());
    m_ocr->setMaxChars(m_settings->ocrMaxChars());
    connect(m_settings, &SettingsManager::changed, this, [this] {
        m_ocr->setLanguage(m_settings->ocrLanguage());
        m_ocr->setMaxChars(m_settings->ocrMaxChars());
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

    // Auto-expire rules: apply at startup + every 15 min + after capture bursts.
    connect(m_watcher, &ClipboardWatcher::captured, m_expire,
            &ExpireScheduler::scheduleAfterCapture);
    connect(m_dataControl, &WlrDataControlHelper::captured, m_expire,
            &ExpireScheduler::scheduleAfterCapture);
    connect(m_expire, &ExpireScheduler::expired, this, [](int count) {
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
    connect(m_tray, &TrayController::quitRequested, qApp, &QCoreApplication::quit);

    connect(m_quickPaste, &QuickPasteMenu::pasteRequested, this,
            [this](qint64 entryId) { pasteEntry(entryId); });

    m_watcher->start();
    m_dataControl->start();
    // Apply the configured theme before any window is shown.
    applyThemes(m_settings->theme(), m_settings->iconTheme(), m_settings->textAppearance());
    if (m_settings->startVisible())
        m_window->show();

    scheduleVacuumChecks();
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
    bool updatedExisting = false;
    const qint64 id = m_storage->insertOrUpdate(record, &updatedExisting);
    if (id == 0)
        return;

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
