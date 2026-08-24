#pragma once

#include <QDialog>
class ApplicationContext;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTextBrowser;

// Detailed settings dialog — 9 tabs: Behaviour / Platform / History & Privacy /
// Search & Preview / Automation / Hotkeys / Storage / Diagnostics.
// Every knob is exposed with live diagnostics, all local, no network.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(ApplicationContext &context, QWidget *parent = nullptr);
private:
    QWidget *buildBehaviourPage();
    QWidget *buildAppearancePage();
    QWidget *buildPlatformPage();
    QWidget *buildHistoryPage();
    QWidget *buildSearchPreviewPage();
    QWidget *buildStoragePage();
    QWidget *buildAutomationPage();
    QWidget *buildHotkeysPage();
    QWidget *buildDiagnosticsPage();
    // legacy names kept for compatibility
    QWidget *buildGeneralPage() { return buildBehaviourPage(); }
    QWidget *buildSearchPage() { return buildSearchPreviewPage(); }

    void load();
    void save();
    void refreshDiagnostics();
    void populateSnippetList();
    void populateTransformList();
    void populateScriptList();
    // Coalesced deferred refresh: safe to call from list-item signal handlers
    // while lists are being rebuilt (avoids re-entrant repaint loops).
    void scheduleDiagnosticsRefresh();

    ApplicationContext &m_ctx;
    // True while the transform/script lists are being cleared+refilled.
    // QListWidget emits itemChanged during insertion; without this guard the
    // itemChanged handlers below re-enter populate*() unboundedly (stack overflow).
    bool m_populatingLists = false;

    // Behaviour
    QCheckBox *m_startVisible = nullptr;
    QCheckBox *m_hideOnFocusOut = nullptr;
    QCheckBox *m_primarySelection = nullptr;
    QCheckBox *m_autostart = nullptr;
    QSpinBox *m_quickPasteCount = nullptr;
    QComboBox *m_trayMode = nullptr;
    QCheckBox *m_notifications = nullptr;

    // Platform
    QLabel *m_platformStatus = nullptr;
    QLabel *m_dataControlStatus = nullptr;
    QLabel *m_platformDetails = nullptr;
    QLabel *m_qpaInfo = nullptr;

    // History & privacy
    QSpinBox *m_debounce = nullptr;
    QRadioButton *m_sensitiveOff = nullptr;
    QRadioButton *m_sensitiveMark = nullptr;
    QRadioButton *m_sensitiveExclude = nullptr;
    QPlainTextEdit *m_customPatterns = nullptr;
    QPushButton *m_testSensitiveBtn = nullptr;
    QLabel *m_sensitiveTestResult = nullptr;
    QSpinBox *m_maxItemMb = nullptr;
    QSpinBox *m_maxImageMb = nullptr;
    QSpinBox *m_diskCapMb = nullptr;
    QWidget *m_storagePage = nullptr;
    QPlainTextEdit *m_ignoredApps = nullptr;
    QCheckBox *m_ocrEnabled = nullptr;
    QComboBox *m_ocrLang = nullptr;
    QSpinBox *m_ocrMaxChars = nullptr;
    QListWidget *m_appSuggestions = nullptr;
    QPushButton *m_addIgnoreBtn = nullptr;

    // Search & preview
    QLabel *m_ftsStatus = nullptr;
    QLabel *m_ocrStatus = nullptr;
    QLabel *m_paletteInfo = nullptr;
    QPushButton *m_ftsRebuildBtn = nullptr;
    QPushButton *m_ftsOptimizeBtn = nullptr;
    QPushButton *m_testOcrBtn = nullptr;
    QCheckBox *m_previewCode = nullptr;
    QCheckBox *m_previewLinks = nullptr;
    QCheckBox *m_previewColors = nullptr;
    QLabel *m_previewSample = nullptr;

    // Automation
    QLabel *m_transformStatus = nullptr;
    QListWidget *m_transformList = nullptr;
    QLabel *m_snippetStatus = nullptr;
    QListWidget *m_snippetList = nullptr;
    QLabel *m_scriptStatus = nullptr;
    QListWidget *m_scriptList = nullptr;

    // Diagnostics
    QTextBrowser *m_diagBrowser = nullptr;
};
