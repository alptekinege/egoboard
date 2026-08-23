#pragma once

#include <QObject>

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
    };
    Q_ENUM(SensitiveMode)

    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() override;

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

    qint64 maxItemBytes() const; // payload cap per entry (text)
    void setMaxItemBytes(qint64 bytes);

    qint64 maxImageBytes() const; // separate cap for images
    void setMaxImageBytes(qint64 bytes);

    qint64 diskCapBytes() const; // 0 = unlimited history
    void setDiskCapBytes(qint64 bytes);

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

    static QString defaultDatabasePath();
    static QString autostartDesktopFilePath();

signals:
    void changed();

private:
    void save();

    KConfig *m_config = nullptr; // KConfig is not a QObject; owned manually
};
