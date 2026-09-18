#pragma once

#include "ClipboardRecord.h"
#include "TextAppearance.h"

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
class SystemThemeWatcher;
class TrayController;
class EncryptionManager;
class VacuumWorker;
class WlrDataControlHelper;
class BackupService;
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
    // Synthetic scale check (--bench): bulk-loads entries into a scratch
    // database and reports page/FTS/insert/export timings against budgets.
    int benchmark(int entryCount);

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
    BackupService *backupService() const { return m_backup; }

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

    // Pauses/resumes recording on both capture paths (tray, global shortcut or
    // the session being locked). Lock pauses are lifted when the lock ends.
    void setCapturePaused(bool paused, bool fromLock = false);
    bool isCapturePaused() const { return m_capturePaused; }

    // Installs a color scheme, an icon theme and the text appearance into the
    // running app (palette, icon search paths, UI font, repaint nudge). Startup,
    // settings changes and Plasma's own theme changes all go through here; the
    // settings dialog calls it for live preview. force re-applies values that did
    // not change, for when Plasma rewrote a scheme file behind them.
    void applyThemes(const QString &colorTheme, const QString &iconTheme,
                     const TextAppearance::Overrides &text, bool force = false);

private slots:
    // DBus ScreenSaver ActiveChanged → pause/resume while locked.
    void onSessionLockChanged(bool locked) { setCapturePaused(locked, true); }

private:
    void onCaptured(const ClipboardRecord &record);
    void scheduleVacuumChecks();
    // One-shot background PRAGMA quick_check; notifies only when it fails.
    void scheduleIntegrityCheck();
    // Subscribes to the session's screen-lock signal for pause-on-lock.
    void watchSessionLock();

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
    QTimer *m_retentionTimer = nullptr;
    OcrWorker *m_ocr = nullptr;
    EncryptionManager *m_encryption = nullptr;
    BackupService *m_backup = nullptr;
    SystemThemeWatcher *m_systemTheme = nullptr;
    // Memoized so saving the settings dialog (one write per key) does not
    // re-apply the appearance once per changed setting.
    QString m_appliedColorTheme;
    QString m_appliedIconTheme;
    TextAppearance::Overrides m_appliedText;
    int m_captureCounter = 0;
    bool m_capturePaused = false;
    bool m_manualPause = false; // set by tray/hotkey, survives lock pauses
    bool m_lockPause = false;
};
