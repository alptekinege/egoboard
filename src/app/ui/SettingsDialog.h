#pragma once

#include "CrashReport.h"
#include "ExpirePolicy.h"

#include <QDialog>
#include <QList>
#include <QPointer>
class ApplicationContext;
class KColorButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QProgressDialog;
class QRadioButton;
class QSpinBox;
class QTextBrowser;
class QVBoxLayout;

// Detailed settings dialog — 9 pages in a vertical icon+label sidebar: General /
// Capture / Privacy / History / Search & Preview / Automation / Shortcuts /
// Storage / Diagnostics. Every knob is exposed with live diagnostics, all
// local, no network.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(ApplicationContext &context, QWidget *parent = nullptr);
private:
    QWidget *buildGeneralPage();
    QWidget *buildCapturePage();
    QWidget *buildPrivacyPage();
    QWidget *buildHistoryPage();
    QWidget *buildSearchPreviewPage();
    QWidget *buildAutomationPage();
    QWidget *buildHotkeysPage();
    QWidget *buildStoragePage();
    QWidget *buildPlatformDiagnosticsPage();

    void load();
    void save();    // Applies a changed encryption checkbox: rekeys the open database to
    // encrypt it (generating a KWallet key when needed) or decrypt it in place.
    // On failure the checkbox is reverted and the setting is left unchanged.
    void applyEncryptionSetting();
    // Live preview of the theme combos: applies without saving, so the user sees
    // the palette/icon theme before committing; Cancel puts the stored pair back.
    void previewThemes();
    void refreshDiagnostics();
    void populateSnippetList();
    void populateTransformList();
    void populateScriptList();
    // U13 DB-error state (Storage ▸ Maintenance): persistent integrity panel.
    void rebuildSearchIndex();
    void showIntegrityError(const QString &error);
    void hideIntegrityError();
    // U11 (G9) backup/restore progress: one at a time (the service runs a
    // single worker op), closed by the finished handlers below.
    void showIoProgress(const QString &label);
    void closeIoProgress();
    // Crash-report toolkit (U20): same schema as the CLI modes.
    void createCrashReport();
    void openCrashReport();
    void previewCrashReport(const CrashReport::Data &data, const QString &saveDir);
    // Redact mode: sync kind-toggle enabled state with the selected radio.
    void updateRedactUi();
    // Expire-rules editor: rebuild the list widget from m_expireRules.
    void refreshExpireList();
    // Coalesced deferred refresh: safe to call from list-item signal handlers
    // while lists are being rebuilt (avoids re-entrant repaint loops).
    void scheduleDiagnosticsRefresh();

    ApplicationContext &m_ctx;
    // True while the transform/script lists are being cleared+refilled.
    // QListWidget emits itemChanged during insertion; without this guard the
    // itemChanged handlers below re-enter populate*() unboundedly (stack overflow).
    bool m_populatingLists = false;

    // General
    QCheckBox *m_startVisible = nullptr;
    QCheckBox *m_hideOnFocusOut = nullptr;
    QCheckBox *m_autostart = nullptr;
    QComboBox *m_trayMode = nullptr;
    QComboBox *m_trayPrimaryClick = nullptr;
    QComboBox *m_traySecondaryClick = nullptr;
    QCheckBox *m_trayWheelCycles = nullptr;
    QCheckBox *m_notifications = nullptr;
    QCheckBox *m_captureSound = nullptr;      // U16
    QCheckBox *m_captureNotification = nullptr; // U16
    QLineEdit *m_autostartCommand = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_iconThemeCombo = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QComboBox *m_textColorCombo = nullptr;
    KColorButton *m_textColorButton = nullptr;
    QComboBox *m_dimTextColorCombo = nullptr;
    KColorButton *m_dimTextColorButton = nullptr;
    QComboBox *m_densityCombo = nullptr;
    QCheckBox *m_toolbarIconOnly = nullptr;
    QCheckBox *m_reduceMotion = nullptr;
    QCheckBox *m_groupByDay = nullptr;
    QCheckBox *m_showEntryIndex = nullptr;
    QCheckBox *m_showUseCountBadge = nullptr;
    QCheckBox *m_privacyBlur = nullptr;
    QCheckBox *m_closeAfterPaste = nullptr;
    QCheckBox *m_bumpOnPaste = nullptr;
    QCheckBox *m_pasteAsPlainText = nullptr;
    QCheckBox *m_rememberGeometry = nullptr;
    QCheckBox *m_restoreFilter = nullptr;
    QComboBox *m_timestampCombo = nullptr;
    QCheckBox *m_clock24h = nullptr;

    // Capture
    QCheckBox *m_primarySelection = nullptr;
    QList<QCheckBox *> m_captureTypeBoxes; // one per recorded ContentType
    QCheckBox *m_pauseOnLock = nullptr;
    QSpinBox *m_quickPasteCount = nullptr;
    QCheckBox *m_quickPasteTwoLine = nullptr;
    QSpinBox *m_debounce = nullptr;
    QSpinBox *m_maxItemMb = nullptr;
    QSpinBox *m_maxImageMb = nullptr;
    QPlainTextEdit *m_ignoredApps = nullptr;
    QListWidget *m_appSuggestions = nullptr;
    QPushButton *m_addIgnoreBtn = nullptr;

    // Privacy
    QRadioButton *m_sensitiveOff = nullptr;
    QRadioButton *m_sensitiveMark = nullptr;
    QRadioButton *m_sensitiveRedact = nullptr;
    QRadioButton *m_sensitiveExclude = nullptr;
    QList<QCheckBox *> m_redactKindBoxes; // one per SensitiveDataDetector::allKinds()
    QPushButton *m_redactTestBtn = nullptr;
    QLabel *m_redactTestResult = nullptr;
    QPlainTextEdit *m_customPatterns = nullptr;
    QPushButton *m_testSensitiveBtn = nullptr;
    QLabel *m_sensitiveTestResult = nullptr;
    QCheckBox *m_encryptionEnabled = nullptr;
    QLabel *m_encryptionStatus = nullptr;
    QPushButton *m_encryptionSetupBtn = nullptr;
    QPushButton *m_encryptionRemoveBtn = nullptr;

    // Storage page: automatic backups.
    QCheckBox *m_backupEnabled = nullptr;
    QLineEdit *m_backupFolder = nullptr;
    QSpinBox *m_backupKeep = nullptr;
    QPushButton *m_backupNowBtn = nullptr;
    QLabel *m_backupStatus = nullptr;
    // U11 (G9) cancelable backup/restore progress dialog for manual runs
    // (automatic runs only touch the status label).
    QPointer<QProgressDialog> m_ioProgress;
    // U13 DB-error state: persistent integrity panel inside Maintenance.
    QVBoxLayout *m_maintenanceLayout = nullptr;
    QWidget *m_integrityError = nullptr;

    // History (retention)
    QSpinBox *m_maxEntries = nullptr;
    QSpinBox *m_diskCapMb = nullptr;
    QListWidget *m_expireList = nullptr;
    QComboBox *m_expireType = nullptr;
    QLineEdit *m_expireApp = nullptr;
    QSpinBox *m_expireAgeH = nullptr;
    QCheckBox *m_expireKeepPinned = nullptr;
    QList<ExpireRule> m_expireRules; // working copy, written on save

    // Search & preview
    QLabel *m_ftsStatus = nullptr;
    QPushButton *m_ftsRebuildBtn = nullptr;
    QPushButton *m_ftsOptimizeBtn = nullptr;
    QCheckBox *m_previewCode = nullptr;
    QCheckBox *m_previewLinks = nullptr;
    QCheckBox *m_previewColors = nullptr;
    QLabel *m_previewSample = nullptr;
    QCheckBox *m_timelineEnabled = nullptr;
    QCheckBox *m_ocrEnabled = nullptr;
    QComboBox *m_ocrLang = nullptr;
    QSpinBox *m_ocrMaxChars = nullptr;
    QLabel *m_ocrStatus = nullptr;
    QPushButton *m_testOcrBtn = nullptr;

    // Automation
    QLabel *m_transformStatus = nullptr;
    QListWidget *m_transformList = nullptr;
    QLabel *m_snippetStatus = nullptr;
    QListWidget *m_snippetList = nullptr;
    QLabel *m_snippetShortcutProblems = nullptr;
    QLabel *m_scriptStatus = nullptr;
    QListWidget *m_scriptList = nullptr;

    // Platform & diagnostics
    QLabel *m_platformStatus = nullptr;
    QLabel *m_dataControlStatus = nullptr;
    QLabel *m_platformDetails = nullptr;
    QTextBrowser *m_diagBrowser = nullptr;
};
