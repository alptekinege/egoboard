#pragma once

#include "ClipboardRecord.h"

#include <QObject>
#include <QThread>

#include <memory>

class AutoPaster;
class BookmarkManager;
class ClipboardWatcher;
class EgoboardDbusAdaptor;
class ExpireScheduler;
class ExportImportManager;
class HotkeyManager;
class IActiveWindowTracker;
class MainWindow;
class QuickPasteMenu;
class QMenu;
class QTimer;
class ScriptActionManager;
class SettingsManager;
class SnippetManager;
class StorageManager;
class TrayController;
class EncryptionManager;
class VacuumWorker;
class WlrDataControlHelper;
class OcrWorker;

// Composition root: owns every subsystem and wires the signal/slot graph.
// fullGui == false gives a headless configuration (storage/bookmarks/io only)
// used by the --smoke self-check.
class ApplicationContext : public QObject {
    Q_OBJECT
public:
    explicit ApplicationContext(const QString &databasePath, bool fullGui = true,
                                QObject *parent = nullptr);
    ~ApplicationContext() override;

    void start();
    int smokeTest();

    SettingsManager *settings() const { return m_settings; }
    StorageManager *storage() const { return m_storage; }
    BookmarkManager *bookmarks() const { return m_bookmarks; }
    ExportImportManager *io() const { return m_io; }
    SnippetManager *snippets() const { return m_snippets; }
    ScriptActionManager *scripts() const { return m_scripts; }
    WlrDataControlHelper *dataControl() const { return m_dataControl; }
    AutoPaster *autoPaster() const { return m_paster; }
    HotkeyManager *hotkeys() const { return m_hotkeys; }
    MainWindow *window() const { return m_window.get(); }
    QuickPasteMenu *quickPaste() const { return m_quickPaste; }

    enum class PasteVariant {
        Normal, // as stored (still honors "always paste as plain text")
        PlainText, // strip HTML formatting
        UpperCase,
        LowerCase,
        WithTimestamp, // prepend "[yyyy-MM-dd HH:mm] "
        ImageAsPngFile, // save the image to a temp PNG, paste as a file
    };

    void toggleMainWindow();
    void showQuickPaste();
    void pasteEntry(qint64 entryId, PasteVariant variant = PasteVariant::Normal);
    void deleteLastEntry(); // drop the newest capture (global hotkey)
    void vacuumNow();

private:
    void onCaptured(const ClipboardRecord &record);
    void scheduleVacuumChecks();

    bool m_fullGui = true;

    SettingsManager *m_settings = nullptr;
    StorageManager *m_storage = nullptr;
    BookmarkManager *m_bookmarks = nullptr;
    ExportImportManager *m_io = nullptr;
    SnippetManager *m_snippets = nullptr;
    ScriptActionManager *m_scripts = nullptr;
    EgoboardDbusAdaptor *m_dbus = nullptr;
    std::shared_ptr<IActiveWindowTracker> m_tracker;
    ClipboardWatcher *m_watcher = nullptr;
    WlrDataControlHelper *m_dataControl = nullptr;
    ExpireScheduler *m_expire = nullptr;
    AutoPaster *m_paster = nullptr;
    HotkeyManager *m_hotkeys = nullptr;
    TrayController *m_tray = nullptr;
    std::unique_ptr<MainWindow> m_window;
    QuickPasteMenu *m_quickPaste = nullptr;

    VacuumWorker *m_vacuumWorker = nullptr;
    QThread *m_vacuumThread = nullptr;
    QTimer *m_vacuumTimer = nullptr;
    OcrWorker *m_ocr = nullptr;
    EncryptionManager *m_encryption = nullptr;
    int m_captureCounter = 0;
};
