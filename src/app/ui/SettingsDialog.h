#pragma once

#include <QDialog>

class ApplicationContext;
class QCheckBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

// Detailed settings dialog with tabs: General / History & Privacy /
// Search & Palette / Hotkeys / Storage / Automation (Phase 3). Provides inline diagnostics for FTS,
// OCR, transforms, snippets, and per-app rules — all local, no network.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(ApplicationContext &context, QWidget *parent = nullptr);

private:
    QWidget *buildGeneralPage();
    QWidget *buildStoragePage();
    QWidget *buildSearchPage();
    QWidget *buildAutomationPage();
    void load();
    void save();
    void refreshDiagnostics();

    ApplicationContext &m_ctx;

    // General
    QCheckBox *m_startVisible = nullptr;
    QCheckBox *m_hideOnFocusOut = nullptr;
    QCheckBox *m_primarySelection = nullptr;
    QCheckBox *m_autostart = nullptr;
    QSpinBox *m_quickPasteCount = nullptr;

    // History & privacy
    QSpinBox *m_debounce = nullptr;
    QRadioButton *m_sensitiveOff = nullptr;
    QRadioButton *m_sensitiveMark = nullptr;
    QRadioButton *m_sensitiveExclude = nullptr;
    QSpinBox *m_maxItemMb = nullptr;
    QSpinBox *m_diskCapMb = nullptr;
    QWidget *m_storagePage = nullptr;
    QPlainTextEdit *m_ignoredApps = nullptr;
    QCheckBox *m_ocrEnabled = nullptr;
    QListWidget *m_appSuggestions = nullptr;

    // Search & diagnostics
    QLabel *m_ftsStatus = nullptr;
    QLabel *m_ocrStatus = nullptr;
    QLabel *m_paletteInfo = nullptr;
    QPushButton *m_ftsRebuildBtn = nullptr;
    QPushButton *m_testOcrBtn = nullptr;

    // Automation (Phase 3)
    QLabel *m_transformStatus = nullptr;
    QLabel *m_snippetStatus = nullptr;
    QLabel *m_scriptStatus = nullptr;
    QListWidget *m_scriptList = nullptr;
};
