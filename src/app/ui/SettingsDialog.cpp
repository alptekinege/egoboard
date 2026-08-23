#include "SettingsDialog.h"

#include "../ApplicationContext.h"
#include "../HotkeyManager.h"
#include "../SettingsManager.h"
#include "StorageManager.h"

#include <KGlobalAccel>
#include <KKeySequenceWidget>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString humanSize(qint64 bytes)
{
    if (bytes < 1024 * 1024)
        return SettingsDialog::tr("%1 kB").arg(bytes / 1024.0, 'f', 1);
    return SettingsDialog::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 'f', 1);
}
} // namespace

SettingsDialog::SettingsDialog(ApplicationContext &context, QWidget *parent)
    : QDialog(parent)
    , m_ctx(context)
{
    setWindowTitle(tr("Egoboard Settings"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralPage(), tr("General"));

    // --- history & privacy ----------------------------------------------------
    auto *historyPage = new QWidget(this);
    auto *historyLayout = new QVBoxLayout(historyPage);

    auto *captureBox = new QGroupBox(tr("Capture"), historyPage);
    auto *captureForm = new QFormLayout(captureBox);
    m_debounce = new QSpinBox(captureBox);
    m_debounce->setRange(50, 5000);
    m_debounce->setSingleStep(50);
    m_debounce->setSuffix(tr(" ms"));
    captureForm->addRow(tr("Debounce interval:"), m_debounce);

    m_maxItemMb = new QSpinBox(captureBox);
    m_maxItemMb->setRange(0, 512);
    m_maxItemMb->setSpecialValueText(tr("No limit"));
    m_maxItemMb->setSuffix(tr(" MB"));
    captureForm->addRow(tr("Max size per entry:"), m_maxItemMb);
    captureForm->addRow(
        tr(""),
        new QLabel(tr("Oversized images are not stored; oversized text is truncated."),
                   captureBox));
    historyLayout->addWidget(captureBox);

    auto *privacyBox = new QGroupBox(tr("Sensitive data"), historyPage);
    auto *privacyLayout = new QVBoxLayout(privacyBox);
    m_sensitiveOff = new QRadioButton(tr("Keep everything without checks"), privacyBox);
    m_sensitiveMark = new QRadioButton(tr("Store but mark (credit cards, passwords, tokens)…"),
                                       privacyBox);
    m_sensitiveExclude = new QRadioButton(tr("Never store sensitive content"), privacyBox);
    privacyLayout->addWidget(m_sensitiveOff);
    privacyLayout->addWidget(m_sensitiveMark);
    privacyLayout->addWidget(m_sensitiveExclude);
    historyLayout->addWidget(privacyBox);

    auto *rulesBox = new QGroupBox(tr("Per-app rules & OCR"), historyPage);
    auto *rulesLayout = new QVBoxLayout(rulesBox);
    auto *ignoredLabel = new QLabel(tr("Ignore clipboard from these apps (one per line, supports * wildcard):"), rulesBox);
    ignoredLabel->setWordWrap(true);
    rulesLayout->addWidget(ignoredLabel);
    m_ignoredApps = new QPlainTextEdit(rulesBox);
    m_ignoredApps->setPlaceholderText(tr("e.g.\norg.keepassxc.KeePassXC\n1Password\nfirefox*\ncom.github.*"));
    m_ignoredApps->setMaximumHeight(90);
    rulesLayout->addWidget(m_ignoredApps);
    auto *ignoredHint = new QLabel(tr("Source app comes from the active window tracker (X11: process name, Wayland: app_id). Leave empty to capture everything."), rulesBox);
    ignoredHint->setWordWrap(true);
    ignoredHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    rulesLayout->addWidget(ignoredHint);
    m_ocrEnabled = new QCheckBox(tr("Enable OCR for images (local tesseract, searchable text)"), rulesBox);
    m_ocrEnabled->setToolTip(tr("When enabled, copied images are OCR'd in the background and become searchable. Requires tesseract installed (no network)."));
    rulesLayout->addWidget(m_ocrEnabled);
    historyLayout->addWidget(rulesBox);

    historyLayout->addStretch(1);
    tabs->addTab(historyPage, tr("History && Privacy"));

    // --- hotkeys ---------------------------------------------------------------
    auto *hotkeyPage = new QWidget(this);
    auto *hotkeyLayout = new QVBoxLayout(hotkeyPage);
    auto *hotkeyBox = new QGroupBox(tr("Global shortcuts"), hotkeyPage);
    auto *hotkeyForm = new QFormLayout(hotkeyBox);

    auto *toggleKey = new KKeySequenceWidget(hotkeyBox);
    toggleKey->setKeySequence(
        KGlobalAccel::self()->shortcut(m_ctx.hotkeys()->toggleAction()).value(0));
    connect(toggleKey, &KKeySequenceWidget::keySequenceChanged, this,
            [this, toggleKey](const QKeySequence &sequence) {
                KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->toggleAction(),
                                                  {sequence}, KGlobalAccel::NoAutoloading);
            });
    hotkeyForm->addRow(tr("Show/hide history window:"), toggleKey);

    auto *quickKey = new KKeySequenceWidget(hotkeyBox);
    quickKey->setKeySequence(
        KGlobalAccel::self()->shortcut(m_ctx.hotkeys()->quickPasteAction()).value(0));
    connect(quickKey, &KKeySequenceWidget::keySequenceChanged, this,
            [this, quickKey](const QKeySequence &sequence) {
                KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->quickPasteAction(),
                                                  {sequence}, KGlobalAccel::NoAutoloading);
            });
    hotkeyForm->addRow(tr("Quick paste menu:"), quickKey);

    auto *resetKeys = new QPushButton(tr("Reset to defaults (Meta+V / Meta+Shift+V)"), hotkeyBox);
    connect(resetKeys, &QPushButton::clicked, this, [this, toggleKey, quickKey] {
        KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->toggleAction(),
                                          HotkeyManager::defaultToggleShortcut());
        KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->quickPasteAction(),
                                          HotkeyManager::defaultQuickPasteShortcut());
        toggleKey->setKeySequence(HotkeyManager::defaultToggleShortcut().value(0));
        quickKey->setKeySequence(HotkeyManager::defaultQuickPasteShortcut().value(0));
    });
    hotkeyForm->addRow(QString(), resetKeys);
    hotkeyLayout->addWidget(hotkeyBox);
    hotkeyLayout->addStretch(1);
    tabs->addTab(hotkeyPage, tr("Hotkeys"));

    // --- storage ----------------------------------------------------------------
    tabs->addTab(buildStoragePage(), tr("Storage"));

    layout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this,
            [this] { save(); accept(); });
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    load();
}

QWidget *SettingsDialog::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *startupBox = new QGroupBox(tr("Startup && window"), page);
    auto *startupLayout = new QVBoxLayout(startupBox);
    m_startVisible = new QCheckBox(tr("Show the history window on start"), startupBox);
    m_hideOnFocusOut = new QCheckBox(tr("Hide the window when it loses focus"), startupBox);
    startupLayout->addWidget(m_startVisible);
    startupLayout->addWidget(m_hideOnFocusOut);
    layout->addWidget(startupBox);

    auto *captureBox = new QGroupBox(tr("Capture"), page);
    auto *captureLayout = new QVBoxLayout(captureBox);
    m_primarySelection =
        new QCheckBox(tr("Also monitor the primary selection (middle-click paste)"), captureBox);
    m_quickPasteCount = new QSpinBox(captureBox);
    m_quickPasteCount->setRange(1, 9);
    auto *countRow = new QHBoxLayout();
    countRow->addWidget(new QLabel(tr("Entries in quick paste menu:"), captureBox));
    countRow->addWidget(m_quickPasteCount);
    countRow->addStretch(1);
    captureLayout->addWidget(m_primarySelection);
    captureLayout->addLayout(countRow);
    layout->addWidget(captureBox);

    auto *autostartBox = new QGroupBox(tr("Session"), page);
    auto *autostartLayout = new QVBoxLayout(autostartBox);
    m_autostart = new QCheckBox(tr("Start Egoboard automatically on login "
                                   "(~/.config/autostart)"),
                                autostartBox);
    autostartLayout->addWidget(m_autostart);
    layout->addWidget(autostartBox);

    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildStoragePage()
{
    m_storagePage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_storagePage);

    auto *dbInfoBox = new QGroupBox(tr("Database"), m_storagePage);
    auto *dbLayout = new QVBoxLayout(dbInfoBox);
    const QString path = m_ctx.storage()->databasePath();
    const qint64 size = m_ctx.storage()->databaseFileSize();
    const auto stats = m_ctx.storage()->stats();
    auto *pathLabel = new QLabel(tr("File: %1").arg(path), dbInfoBox);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *sizeLabel = new QLabel(
        tr("%1 on disk · %2 entries · %3 pinned").arg(humanSize(size)).arg(stats.entryCount).arg(
            stats.pinnedCount),
        dbInfoBox);
    dbLayout->addWidget(pathLabel);
    dbLayout->addWidget(sizeLabel);
    layout->addWidget(dbInfoBox);

    auto *limitsBox = new QGroupBox(tr("Limits"), m_storagePage);
    auto *limitsForm = new QFormLayout(limitsBox);
    m_diskCapMb = new QSpinBox(limitsBox);
    m_diskCapMb->setRange(0, 1024 * 64);
    m_diskCapMb->setSpecialValueText(tr("Unlimited"));
    m_diskCapMb->setSuffix(tr(" MB"));
    limitsForm->addRow(tr("Total history size cap:"), m_diskCapMb);
    limitsForm->addRow(
        tr(""),
        new QLabel(tr("When a cap is set, the oldest non-pinned entries are removed to stay "
                      "below it. History is unlimited by default."),
                   limitsBox));
    layout->addWidget(limitsBox);

    auto *maintenanceBox = new QGroupBox(tr("Maintenance"), m_storagePage);
    auto *maintenanceLayout = new QVBoxLayout(maintenanceBox);
    auto *vacuumButton =
        new QPushButton(tr("Compact database now (VACUUM)"), maintenanceBox);
    connect(vacuumButton, &QPushButton::clicked, this, [this, vacuumButton] {
        vacuumButton->setEnabled(false);
        vacuumButton->setText(tr("Compacting…"));
        m_ctx.vacuumNow();
        // VACUUM is fast enough for typical sizes; let the user close manually.
        QTimer::singleShot(3000, this, [vacuumButton] {
            vacuumButton->setText(tr("Compact database now (VACUUM)"));
            vacuumButton->setEnabled(true);
        });
    });
    maintenanceLayout->addWidget(vacuumButton);
    maintenanceLayout->addWidget(
        new QLabel(tr("Egoboard also compacts automatically once a day when the database "
                      "grows past 50 MB."),
                   maintenanceBox));
    layout->addWidget(maintenanceBox);
    layout->addStretch(1);
    return m_storagePage;
}

void SettingsDialog::load()
{
    m_startVisible->setChecked(m_ctx.settings()->startVisible());
    m_hideOnFocusOut->setChecked(m_ctx.settings()->hideOnFocusOut());
    m_primarySelection->setChecked(m_ctx.settings()->monitorPrimarySelection());
    m_quickPasteCount->setValue(m_ctx.settings()->quickPasteCount());
    m_autostart->setChecked(m_ctx.settings()->autostartEnabled());

    m_debounce->setValue(m_ctx.settings()->debounceMs());
    m_maxItemMb->setValue(int(m_ctx.settings()->maxItemBytes() / (1024 * 1024)));
    switch (m_ctx.settings()->sensitiveMode()) {
    case SettingsManager::SensitiveMode::Off:
        m_sensitiveOff->setChecked(true);
        break;
    case SettingsManager::SensitiveMode::Mark:
        m_sensitiveMark->setChecked(true);
        break;
    case SettingsManager::SensitiveMode::Exclude:
        m_sensitiveExclude->setChecked(true);
        break;
    }
    m_diskCapMb->setValue(int(m_ctx.settings()->diskCapBytes() / (1024 * 1024)));
    if (m_ignoredApps) {
        m_ignoredApps->setPlainText(m_ctx.settings()->ignoredSourceApps().join(QStringLiteral("\n")));
    }
    if (m_ocrEnabled) m_ocrEnabled->setChecked(m_ctx.settings()->ocrEnabled());
}

void SettingsDialog::save()
{
    m_ctx.settings()->setStartVisible(m_startVisible->isChecked());
    m_ctx.settings()->setHideOnFocusOut(m_hideOnFocusOut->isChecked());
    m_ctx.settings()->setMonitorPrimarySelection(m_primarySelection->isChecked());
    m_ctx.settings()->setQuickPasteCount(m_quickPasteCount->value());
    m_ctx.settings()->setAutostartEnabled(m_autostart->isChecked());

    m_ctx.settings()->setDebounceMs(m_debounce->value());
    m_ctx.settings()->setMaxItemBytes(qint64(m_maxItemMb->value()) * 1024 * 1024);
    if (m_sensitiveOff->isChecked())
        m_ctx.settings()->setSensitiveMode(SettingsManager::SensitiveMode::Off);
    else if (m_sensitiveMark->isChecked())
        m_ctx.settings()->setSensitiveMode(SettingsManager::SensitiveMode::Mark);
    else
        m_ctx.settings()->setSensitiveMode(SettingsManager::SensitiveMode::Exclude);
    m_ctx.settings()->setDiskCapBytes(qint64(m_diskCapMb->value()) * 1024 * 1024);
    if (m_ignoredApps) {
        const QStringList apps = m_ignoredApps->toPlainText().split(QRegularExpression(QStringLiteral("[\n,]+")), Qt::SkipEmptyParts);
        m_ctx.settings()->setIgnoredSourceApps(apps);
    }
    if (m_ocrEnabled) m_ctx.settings()->setOcrEnabled(m_ocrEnabled->isChecked());
}
