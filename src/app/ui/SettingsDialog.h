#pragma once

#include <QDialog>

class ApplicationContext;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;

// Qt Widgets settings dialog (QTabWidget): General / History & Privacy /
// Hotkeys / Storage. Writes through SettingsManager; KGlobalAccel-backed
// shortcuts are edited inline with KKeySequenceWidget.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(ApplicationContext &context, QWidget *parent = nullptr);

private:
    QWidget *buildGeneralPage();
    QWidget *buildStoragePage();
    void load();
    void save();

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
};
