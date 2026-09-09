#pragma once

#include "ExpirePolicy.h"

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
    QString theme() const; // "system" | "light" | "dark"
    void setTheme(const QString &theme);
    bool toolbarIconOnly() const; // main-window toolbar buttons show icons only
    void setToolbarIconOnly(bool iconOnly);

    // Timeline strip (14-day histogram) above the history list
    bool timelineEnabled() const;
    void setTimelineEnabled(bool enabled);

    bool encryptionEnabled() const;
    void setEncryptionEnabled(bool enabled);

    static QString defaultDatabasePath();
    static QString autostartDesktopFilePath();

signals:
    void changed();

private:
    void save();

    KConfig *m_config = nullptr; // KConfig is not a QObject; owned manually
};
