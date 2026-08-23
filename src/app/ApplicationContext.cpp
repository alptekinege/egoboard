#include "ApplicationContext.h"

#include "AutoPaster.h"
#include "BookmarkManager.h"
#include "ClipboardWatcher.h"
#include "ExportImportManager.h"
#include "HotkeyManager.h"
#include "SettingsManager.h"
#include "StorageManager.h"
#include "TrayController.h"
#include "VacuumWorker.h"
#include "OcrWorker.h"
#include "WaylandActiveWindowTracker.h"
#include "X11ActiveWindowTracker.h"
#include "ui/MainWindow.h"
#include "ui/QuickPasteMenu.h"

#include <KNotification>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QTimer>

#include <memory>

namespace {
constexpr qint64 kVacuumSizeThresholdBytes = 50 * 1024 * 1024;
constexpr int kDiskCapCheckInterval = 25; // captures between cap enforcements
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

    // Vacuum runs on its own thread/connection so the GUI connection stays
    // responsive while the database is being compacted.
    m_vacuumThread = new QThread(this);
    m_vacuumWorker = new VacuumWorker(databasePath);
    m_vacuumWorker->moveToThread(m_vacuumThread);
    connect(m_vacuumThread, &QThread::finished, m_vacuumWorker, &QObject::deleteLater);
    m_vacuumThread->start();

    if (!m_fullGui)
        return;

    if (QGuiApplication::platformName() == QLatin1String("wayland"))
        m_tracker = std::make_unique<WaylandActiveWindowTracker>();
    else
        m_tracker = std::make_unique<X11ActiveWindowTracker>();

    m_watcher = new ClipboardWatcher(QGuiApplication::clipboard(), m_settings, m_tracker.get(),
                                     this);
    m_paster = new AutoPaster(m_watcher, this);
    m_hotkeys = new HotkeyManager(this);
    m_tray = new TrayController(m_storage, this);
    m_window = new MainWindow(*this);
    m_quickPaste = new QuickPasteMenu(m_storage, m_settings->quickPasteCount());
    m_ocr = new OcrWorker(m_storage, this);
    connect(m_ocr, &OcrWorker::recognized, this, [this](qint64 id, const QString &text){
        m_storage->setOcrText(id, text);
    });
}

ApplicationContext::~ApplicationContext()
{
    m_vacuumThread->quit();
    m_vacuumThread->wait(5000);
}

void ApplicationContext::start()
{
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
    connect(m_settings, &SettingsManager::changed, this, [this] {
        m_watcher->setDebounceInterval(m_settings->debounceMs());
    });

    connect(m_hotkeys, &HotkeyManager::toggleRequested, this,
            &ApplicationContext::toggleMainWindow);
    connect(m_hotkeys, &HotkeyManager::quickPasteRequested, this,
            &ApplicationContext::showQuickPaste);

    connect(m_tray, &TrayController::toggleRequested, this, &ApplicationContext::toggleMainWindow);
    connect(m_tray, &TrayController::quickPasteRequested, this, &ApplicationContext::showQuickPaste);
    connect(m_tray, &TrayController::pasteRequested, this, &ApplicationContext::pasteEntry);
    connect(m_tray, &TrayController::quitRequested, qApp, &QCoreApplication::quit);

    connect(m_quickPaste, &QuickPasteMenu::pasteRequested, this, &ApplicationContext::pasteEntry);

    m_watcher->start();
    if (m_settings->startVisible())
        m_window->show();

    scheduleVacuumChecks();
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

    // Optional disk-size cap: check every N captures to amortize the cost.
    const qint64 cap = m_settings->diskCapBytes();
    if (cap > 0 && ++m_captureCounter % kDiskCapCheckInterval == 0)
        m_storage->enforceDiskCap(cap);
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

void ApplicationContext::pasteEntry(qint64 entryId)
{
    ClipboardRecord record;
    if (!m_storage->fetchFull(entryId, &record))
        return;
    QWidget *hideTarget = nullptr;
    if (m_window->isVisible())
        hideTarget = m_window;
    else if (m_quickPaste->isVisible())
        hideTarget = m_quickPaste;
    m_paster->paste(record, hideTarget);
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
    qInfo("egoboard smoke test: OK");
    return 0;
}
