#pragma once

#include "ContentType.h"
#include "ExpirePolicy.h"
#include "TextAppearance.h"

#include <memory>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QStringList>

class KConfig;
// Typed access to ~/.config/egoboardrc (KConfig). Emits changed() so
// components can react without polling.
class SettingsManager : public QObject {
    Q_OBJECT
public:
    enum class SensitiveMode {
        Off = 0,
        Mark = 1, // store but flag in the UI
        Exclude = 2, // never store
        Redact = 3, // store with detected secrets replaced by "••••"
    };
    Q_ENUM(SensitiveMode)

    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() override;

    // Settings schema version: [General] ConfigVersion in egoboardrc.
    // Migrations are forward-only and run once when the file is loaded.
    static int currentConfigVersion();
    int configVersion() const;

    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    SettingsManager(SettingsManager&&) = delete;
    SettingsManager& operator=(SettingsManager&&) = delete;

    // Capture: which content types are recorded (all on by default)
    bool captureText() const;
    void setCaptureText(bool enabled);
    bool captureRichText() const;
    void setCaptureRichText(bool enabled);
    bool captureImages() const;
    void setCaptureImages(bool enabled);
    bool captureFiles() const;
    void setCaptureFiles(bool enabled);
    // True when the configured capture filters accept this content type.
    // Shared by both capture paths (QClipboard watcher and wlr-data-control).
    bool captureTypeEnabled(ContentType type) const;

    // Pause recording while the session is locked (default on).
    bool pauseOnLock() const;
    void setPauseOnLock(bool pause);

    // What a tray click does. Values are stable (stored in the config).
    enum class TrayClick {
        ShowWindow = 0,
        QuickPaste = 1,
        TogglePause = 2,
        Nothing = 3,
    };
    TrayClick trayPrimaryClick() const; // left click
    void setTrayPrimaryClick(TrayClick action);
    TrayClick traySecondaryClick() const; // middle click
    void setTraySecondaryClick(TrayClick action);

    // Scrolling over the tray icon walks the recent entries (Klipper-like).
    bool trayWheelCycles() const;
    void setTrayWheelCycles(bool enabled);

    // General
    bool startVisible() const;
    void setStartVisible(bool visible);

    bool hideOnFocusOut() const;
    void setHideOnFocusOut(bool hide);

    bool monitorPrimarySelection() const;
    void setMonitorPrimarySelection(bool monitor);

    int quickPasteCount() const; // 1..9
    void setQuickPasteCount(int count);
    // R3 quick-paste 2.0 (all persist per screen where it makes sense).
    bool quickPasteTwoLine() const; // two-line rows: preview + meta
    void setQuickPasteTwoLine(bool enabled);
    QPoint quickPastePos(const QString &screen) const; // per-screen placement memory
    void setQuickPastePos(const QString &screen, const QPoint &pos);
    void clearQuickPastePos(const QString &screen);

    bool autostartEnabled() const;
    void setAutostartEnabled(bool enabled); // also writes/removes the .desktop entry
    // Rewrites the autostart entry when it does not point at the right binary, so
    // an entry from an older build (or a bare command name, which the session's
    // systemd autostart generator cannot resolve) repairs itself on next start.
    void ensureAutostartEntry();
    // Executable the autostart entry launches when nothing else is chosen: the
    // running binary, or the .AppImage file when running from one (its mount path
    // is temporary).
    static QString autostartExecutablePath();
    // Explicit choice for the autostart entry (an AppImage, or an installed
    // copy); empty means "whatever binary is running".
    QString autostartCommand() const;
    void setAutostartCommand(const QString &path);
    // What the entry actually gets: the chosen command while the file is there
    // and executable, otherwise the running binary.
    QString effectiveAutostartCommand() const;

    // History / privacy
    int debounceMs() const;
    void setDebounceMs(int ms);

    SensitiveMode sensitiveMode() const;
    void setSensitiveMode(SensitiveMode mode);

    // Which built-in kinds are redacted in Redact mode ("creditcard",
    // "credential", "api-key", ...). Empty list = redact every kind.
    QStringList redactKinds() const;
    void setRedactKinds(const QStringList &kinds);

    // Auto-expire rules (Track E). Stored as one encoded string per rule.
    QList<ExpireRule> expireRules() const;
    void setExpireRules(const QList<ExpireRule> &rules);

    qint64 maxItemBytes() const; // payload cap per entry (text)
    void setMaxItemBytes(qint64 bytes);

    qint64 maxImageBytes() const; // separate cap for images
    void setMaxImageBytes(qint64 bytes);

    qint64 diskCapBytes() const; // 0 = unlimited history
    void setDiskCapBytes(qint64 bytes);

    // Entry-count retention cap (0 = unlimited). Oldest non-pinned entries
    // are removed when the history grows past it.
    int maxEntries() const;
    void setMaxEntries(int maxEntries);

    QStringList ignoredSourceApps() const;
    void setIgnoredSourceApps(const QStringList &apps);
    bool isSourceIgnored(const QString &app) const;

    QStringList customSensitivePatterns() const;
    void setCustomSensitivePatterns(const QStringList &patterns);

    bool ocrEnabled() const;
    void setOcrEnabled(bool enabled);

    QString ocrLanguage() const; // e.g. "eng"
    void setOcrLanguage(const QString &lang);

    int ocrMaxChars() const;
    void setOcrMaxChars(int chars);

    // Preview enrichments
    bool previewCodeHighlight() const;
    void setPreviewCodeHighlight(bool enabled);
    bool previewLinkify() const;
    void setPreviewLinkify(bool enabled);
    bool previewColorSwatches() const;
    void setPreviewColorSwatches(bool enabled);

    // Automation
    QStringList disabledScripts() const;
    void setDisabledScripts(const QStringList &ids);
    bool isScriptDisabled(const QString &id) const;
    void setScriptDisabled(const QString &id, bool disabled);

    QStringList hiddenTransforms() const;
    void setHiddenTransforms(const QStringList &names);
    bool isTransformHidden(const QString &name) const;

    // UI
    QString trayMode() const; // "auto" | "always" | "hidden"
    void setTrayMode(const QString &mode);
    bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);
    bool captureSoundEnabled() const;      // U16: play a sound on new copy
    void setCaptureSoundEnabled(bool enabled);
    bool captureNotificationEnabled() const; // U16: show a notification on new copy
    void setCaptureNotificationEnabled(bool enabled);

    // Appearance
    QString theme() const; // "system" | "light" | "dark" | installed color scheme id
    void setTheme(const QString &theme);
    QString iconTheme() const; // "system" | installed icon theme id
    void setIconTheme(const QString &theme);
    // Text readability: size delta plus optional color overrides.
    int fontPointDelta() const; // -2..+6 pt, 0 = platform default
    void setFontPointDelta(int delta);
    QString textColor() const; // "#rrggbb", empty = follow the color scheme
    void setTextColor(const QString &color);
    QString dimTextColor() const; // secondary text, empty = follow the color scheme
    void setDimTextColor(const QString &color);
    // Everything the theme applier needs to keep the UI readable.
    TextAppearance::Overrides textAppearance() const;
    bool toolbarIconOnly() const; // main-window toolbar buttons show icons only
    void setToolbarIconOnly(bool iconOnly);
    // Skips the (already capped) popup fades and the timeline hover transition.
    bool reduceMotion() const;
    void setReduceMotion(bool reduce);

    // Timeline strip (14-day histogram) above the history list
    bool timelineEnabled() const;
    void setTimelineEnabled(bool enabled);

    // R2 list options (all default off): group-by-day headers, row extras.
    bool groupByDay() const;
    void setGroupByDay(bool enabled);
    bool showEntryIndex() const;
    void setShowEntryIndex(bool show);
    bool showUseCountBadge() const;
    void setShowUseCountBadge(bool show);
    bool privacyBlur() const; // blur payload previews until hover/focus
    void setPrivacyBlur(bool blur);

    // Pasting
    bool closeAfterPaste() const; // hide the egoboard window when pasting (default on)
    void setCloseAfterPaste(bool close);
    bool bumpOnPaste() const; // move the pasted entry back to the top (default on)
    void setBumpOnPaste(bool bump);
    bool pasteAsPlainText() const; // strip HTML formatting on paste (default off)
    void setPasteAsPlainText(bool plain);

    // History list density: "compact" | "comfortable" | "spacious"
    QString listDensity() const;
    void setListDensity(const QString &density);

    // History list sort order: 0 = newest, 1 = oldest, 2 = most used
    int sortMode() const; // 0..2, invalid values normalize to 0
    void setSortMode(int mode);

    // Search scope: 0 = all indexed text, 1 = preview, 2 = full text, 3 = OCR.
    int searchScope() const; // invalid values normalize to 0
    void setSearchScope(int scope);

    // Committed search queries, newest first (capped at 10).
    QStringList recentSearches() const;
    void addRecentSearch(const QString &query); // moves an existing entry to the front
    void clearRecentSearches();

    // Palette commands that were executed, most recent first (cap 10).
    QStringList recentPaletteCommands() const;
    void addRecentPaletteCommand(const QString &commandId);

    // --- automatic backups ---------------------------------------------------
    bool backupsEnabled() const;
    void setBackupsEnabled(bool enabled);
    QString backupFolder() const; // empty = use defaultBackupFolder()
    void setBackupFolder(const QString &folder);
    int backupKeep() const; // 1..100
    void setBackupKeep(int keep);
    // When the last automatic backup ran (0 = never); used to avoid re-running
    // on every start.
    qint64 lastBackupMs() const;
    void setLastBackupMs(qint64 ms);
    // ~/Documents/egoboard-backups when a Documents dir exists, else the app
    // data dir.
    static QString defaultBackupFolder();

    // Timestamp rendering in the list: "relative" | "absolute"
    QString timestampStyle() const;
    void setTimestampStyle(const QString &style);
    bool clock24h() const; // 24-hour clock in timestamps (default on)
    void setClock24h(bool enable);

    // Window geometry / session state
    bool rememberWindowGeometry() const; // size, position, splitter (default on)
    void setRememberWindowGeometry(bool remember);
    bool restoreLastFilter() const; // re-apply the last filter on start (default off)
    void setRestoreLastFilter(bool restore);
    // U15 first-run tour: shown once on first launch only (default false =
    // unseen). Skipped or finished, the flag is set so the tour never
    // re-shows without asking (More menu ▸ Tour, palette `>tour`). Onboarding
    // state stays machine-local like lastBackupMs: it never travels in
    // settings export/import or profiles.
    bool tourSeen() const;
    void setTourSeen(bool seen);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);
    QByteArray splitterState() const;
    void setSplitterState(const QByteArray &state);
    // Per-mode splitter state (R1): Wide keeps the legacy key above; Medium
    // and Narrow persist their own so mode switches restore, not reset.
    QByteArray splitterStateForMode(int mode) const; // 0 = Wide, 1 = Medium, 2 = Narrow
    void setSplitterStateForMode(int mode, const QByteArray &state);
    QString lastFilter() const; // serialized FilterSpec JSON
    void setLastFilter(const QString &filterJson);

    bool encryptionEnabled() const;
    void setEncryptionEnabled(bool enabled);

    // U14 settings portability: full snapshot of the preferences above as one
    // JSON object (`format`/`version` envelope + one key per setting), so a
    // setup moves between machines with Export/Import next to the history
    // backup. Round-trips through the setters, so range/enum/theme validation
    // re-runs on import. Deliberately excluded: lastBackupMs (backup-schedule
    // state that must not suppress the first backup on the new machine).
    // Unknown keys are ignored (forward compatible); a wrong format tag or a
    // newer version rejects the file. Emits a single changed().
    static QString settingsFormatTag();
    static int settingsFormatVersion();
    QJsonObject exportToJson() const;
    bool importFromJson(const QJsonObject &root, QString *error = nullptr);

    // U14 per-page reset: restores one settings page to its defaults through
    // the validating setters (single changed()). Session/placement/geometry
    // state (quick-paste positions, window/splitter geometry, last filter,
    // recents, lastBackupMs) and the encryption flag are never touched: the
    // former is not shown as knobs, the latter needs its confirm + rekey flow.
    enum class SettingsPage {
        General,
        Capture,
        Privacy,
        History,
        SearchPreview,
        Automation,
        Storage,
    };
    void resetPageToDefaults(SettingsPage page);

    // U14 profiles ("Work"/"Personal"): named setting sets. Each profile is
    // its own KConfig group ("Profile <name>") holding a JSON snapshot of
    // exportToJson(), so switching routes through importFromJson with the
    // same validation, machine-local exclusions and single changed().
    // History data and KWallet secrets never enter a profile. Saving or
    // deleting a profile only syncs (no changed(): live settings untouched);
    // applying emits the single changed() of the import. ActiveProfile tracks
    // the last saved/applied profile; manual edits do not clear it.
    static QString profileGroupPrefix();
    static bool isValidProfileName(const QString &name);
    static QString normalizeProfileName(const QString &name);
    QStringList profileNames() const; // sorted case-insensitively
    bool hasProfile(const QString &name) const; // exact match after trim
    QString activeProfile() const;
    bool saveProfile(const QString &name, QString *error = nullptr);
    bool applyProfile(const QString &name, QString *error = nullptr);
    bool deleteProfile(const QString &name, QString *error = nullptr);

    static QString defaultDatabasePath();
    static QString autostartDesktopFilePath();

signals:
    void changed();

private:
    void save();
    void migrateConfig(); // forward-only, runs once when the file is loaded

    KConfig *m_config = nullptr; // KConfig is not a QObject; owned manually
    bool m_suppressChanged = false; // import batches many setters into one changed()
};
