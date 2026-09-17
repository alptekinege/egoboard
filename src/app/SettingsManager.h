#pragma once

#include "ContentType.h"
#include "ExpirePolicy.h"
#include "TextAppearance.h"

#include <memory>
#include <QList>
#include <QObject>
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

    // General
    bool startVisible() const;
    void setStartVisible(bool visible);

    bool hideOnFocusOut() const;
    void setHideOnFocusOut(bool hide);

    bool monitorPrimarySelection() const;
    void setMonitorPrimarySelection(bool monitor);

    int quickPasteCount() const; // 1..9
    void setQuickPasteCount(int count);

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

    // Timeline strip (14-day histogram) above the history list
    bool timelineEnabled() const;
    void setTimelineEnabled(bool enabled);

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
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);
    QByteArray splitterState() const;
    void setSplitterState(const QByteArray &state);
    QString lastFilter() const; // serialized FilterSpec JSON
    void setLastFilter(const QString &filterJson);

    bool encryptionEnabled() const;
    void setEncryptionEnabled(bool enabled);

    static QString defaultDatabasePath();
    static QString autostartDesktopFilePath();

signals:
    void changed();

private:
    void save();
    void migrateConfig(); // forward-only, runs once when the file is loaded

    KConfig *m_config = nullptr; // KConfig is not a QObject; owned manually
};
