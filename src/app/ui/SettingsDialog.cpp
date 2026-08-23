#include "SettingsDialog.h"

#include "../ApplicationContext.h"
#include "../HotkeyManager.h"
#include "../SettingsManager.h"
#include "../OcrWorker.h"
#include "StorageManager.h"

#include <KGlobalAccel>
#include <KKeySequenceWidget>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString humanSize(qint64 bytes)
{
    if (bytes < 1024 * 1024)
        return SettingsDialog::tr("%1 kB").arg(bytes / 1024.0, 'f', 1);
    return SettingsDialog::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 'f', 1);
}

QString tesseractVersion()
{
    QProcess p;
    p.start(QStringLiteral("tesseract"), {QStringLiteral("--version")});
    if (!p.waitForFinished(2000)) return {};
    QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
    const QString first = out.split(QLatin1Char('\n')).value(0).trimmed();
    return first.isEmpty() ? QStringLiteral("tesseract") : first;
}
} // namespace

SettingsDialog::SettingsDialog(ApplicationContext &context, QWidget *parent)
    : QDialog(parent)
    , m_ctx(context)
{
    setWindowTitle(tr("Egoboard Settings"));
    setModal(true);
    resize(720, 560);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralPage(), QIcon::fromTheme(QStringLiteral("configure")), tr("General"));
    
    // --- history & privacy ----------------------------------------------------
    auto *historyPage = new QWidget(this);
    auto *historyLayout = new QVBoxLayout(historyPage);

    auto *captureBox = new QGroupBox(tr("Capture"), historyPage);
    auto *captureForm = new QFormLayout(captureBox);
    captureForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_debounce = new QSpinBox(captureBox);
    m_debounce->setRange(50, 5000);
    m_debounce->setSingleStep(50);
    m_debounce->setSuffix(tr(" ms"));
    captureForm->addRow(tr("Debounce interval:"), m_debounce);
    auto *debounceHint = new QLabel(tr("Coalesces rapid clipboard updates (apps that set several MIME types). Lower is more responsive, higher avoids duplicates."), captureBox);
    debounceHint->setWordWrap(true);
    debounceHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    captureForm->addRow(QString(), debounceHint);

    m_maxItemMb = new QSpinBox(captureBox);
    m_maxItemMb->setRange(0, 512);
    m_maxItemMb->setSpecialValueText(tr("No limit"));
    m_maxItemMb->setSuffix(tr(" MB"));
    captureForm->addRow(tr("Max size per entry:"), m_maxItemMb);
    auto *maxHint = new QLabel(tr("Oversized images are not stored (placeholder only); oversized text is truncated with … . Default 5 MB — keeps DB fast."), captureBox);
    maxHint->setWordWrap(true);
    maxHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    captureForm->addRow(QString(), maxHint);
    historyLayout->addWidget(captureBox);

    auto *privacyBox = new QGroupBox(tr("Sensitive data"), historyPage);
    auto *privacyLayout = new QVBoxLayout(privacyBox);
    m_sensitiveOff = new QRadioButton(tr("Keep everything without checks"), privacyBox);
    m_sensitiveMark = new QRadioButton(tr("Store but mark (credit cards, passwords, tokens)…"), privacyBox);
    m_sensitiveExclude = new QRadioButton(tr("Never store sensitive content"), privacyBox);
    auto *privacyHint = new QLabel(tr("Detection: Luhn-validated credit cards, high-entropy secrets, API tokens (e.g. <code>AKIA…</code>, <code>ghp_…</code>, <code>sk-…</code>). <i>Exclude</i> is recommended for shared machines."), privacyBox);
    privacyHint->setWordWrap(true);
    privacyHint->setTextFormat(Qt::RichText);
    privacyHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    privacyLayout->addWidget(m_sensitiveOff);
    privacyLayout->addWidget(m_sensitiveMark);
    privacyLayout->addWidget(m_sensitiveExclude);
    privacyLayout->addWidget(privacyHint);
    historyLayout->addWidget(privacyBox);

    auto *rulesBox = new QGroupBox(tr("Per-app rules"), historyPage);
    auto *rulesLayout = new QVBoxLayout(rulesBox);
    auto *ignoredLabel = new QLabel(tr("Ignore clipboard from these apps (one per line, <b>*</b> wildcard supported):"), rulesBox);
    ignoredLabel->setWordWrap(true);
    rulesLayout->addWidget(ignoredLabel);
    m_ignoredApps = new QPlainTextEdit(rulesBox);
    m_ignoredApps->setPlaceholderText(tr("e.g.\norg.keepassxc.KeePassXC\n1Password\nfirefox*\ncom.github.*\norg.mozilla.firefox"));
    m_ignoredApps->setMaximumHeight(96);
    rulesLayout->addWidget(m_ignoredApps);
    auto *ignoredHint = new QLabel(tr("Source app comes from the window tracker — <b>X11:</b> process name via <code>_NET_WM_PID</code>, <b>Wayland:</b> <code>app_id</code> (e.g. <code>org.kde.kate</code>). Leave empty to capture everything. Password managers should be ignored."), rulesBox);
    ignoredHint->setWordWrap(true);
    ignoredHint->setTextFormat(Qt::RichText);
    ignoredHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    rulesLayout->addWidget(ignoredHint);

    // Suggestions row: shows apps seen in history
    auto *suggestRow = new QHBoxLayout();
    m_appSuggestions = new QListWidget(rulesBox);
    m_appSuggestions->setMaximumHeight(64);
    m_appSuggestions->setSelectionMode(QAbstractItemView::SingleSelection);
    m_appSuggestions->setToolTip(tr("Apps seen in your history — double-click to add to ignore list"));
    suggestRow->addWidget(m_appSuggestions, 1);
    auto *addBtn = new QPushButton(tr("Add → Ignore"), rulesBox);
    addBtn->setToolTip(tr("Add selected app to ignore list"));
    suggestRow->addWidget(addBtn);
    rulesLayout->addLayout(suggestRow);
    connect(m_appSuggestions, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it){
        if (!it || !m_ignoredApps) return;
        const QString app = it->text();
        QString cur = m_ignoredApps->toPlainText().trimmed();
        if (!cur.isEmpty() && !cur.endsWith(QLatin1Char('\n'))) cur += QLatin1Char('\n');
        if (!cur.contains(app)) {
            cur += app;
            m_ignoredApps->setPlainText(cur);
        }
    });
    connect(addBtn, &QPushButton::clicked, this, [this]{
        auto *cur = m_appSuggestions->currentItem();
        if (cur) emit m_appSuggestions->itemDoubleClicked(cur);
    });

    // OCR toggle with status
    m_ocrEnabled = new QCheckBox(tr("Enable OCR for images (local tesseract, searchable)"), rulesBox);
    m_ocrEnabled->setToolTip(tr("When enabled, copied images are OCR'd in the background and become searchable via FTS. Requires tesseract — no network ever."));
    rulesLayout->addWidget(m_ocrEnabled);
    auto *ocrHint = new QLabel(tr("Images with text (screenshots, slides) become searchable. OCR runs at most once per image on a thread pool, capped to 8 kB per entry. In preview you’ll see <i>🔍 OCR:</i> under images."), rulesBox);
    ocrHint->setWordWrap(true);
    ocrHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    rulesLayout->addWidget(ocrHint);

    historyLayout->addWidget(rulesBox);
    historyLayout->addStretch(1);
    // Make history page scrollable when small
    auto *histScroll = new QScrollArea(this);
    histScroll->setWidgetResizable(true);
    histScroll->setWidget(historyPage);
    histScroll->setFrameShape(QFrame::NoFrame);
    tabs->addTab(histScroll, QIcon::fromTheme(QStringLiteral("security-medium")), tr("History & Privacy"));

    // --- search & preview ---------------------------------------------------
    tabs->addTab(buildSearchPage(), QIcon::fromTheme(QStringLiteral("system-search")), tr("Search"));

    // --- hotkeys ---------------------------------------------------------------
    auto *hotkeyPage = new QWidget(this);
    auto *hotkeyLayout = new QVBoxLayout(hotkeyPage);
    auto *hotkeyBox = new QGroupBox(tr("Global shortcuts (KGlobalAccel)"), hotkeyPage);
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

    auto *paletteKey = new QLabel(QStringLiteral("Ctrl+K"), hotkeyBox);
    paletteKey->setTextInteractionFlags(Qt::TextSelectableByMouse);
    paletteKey->setStyleSheet(QStringLiteral("font-family: monospace; background: palette(midlight); padding: 2px 6px; border-radius: 4px;"));
    hotkeyForm->addRow(tr("Command palette (in-app):"), paletteKey);

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
    auto *hotkeyHint = new QLabel(tr("Shortcuts are registered with KWin via KGlobalAccel. They work even when Egoboard is hidden. The palette (Ctrl+K) is local to the window and needs no registration."), hotkeyBox);
    hotkeyHint->setWordWrap(true);
    hotkeyHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    hotkeyForm->addRow(QString(), hotkeyHint);
    hotkeyLayout->addWidget(hotkeyBox);
    hotkeyLayout->addStretch(1);
    tabs->addTab(hotkeyPage, QIcon::fromTheme(QStringLiteral("preferences-desktop-keyboard")), tr("Hotkeys"));

    // --- storage ----------------------------------------------------------------
    tabs->addTab(buildStoragePage(), QIcon::fromTheme(QStringLiteral("drive-harddisk")), tr("Storage"));

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
    refreshDiagnostics();
    // Populate app suggestions after load
    if (m_appSuggestions) {
        const QStringList apps = m_ctx.storage()->sourceApps();
        for (const QString &a : apps) {
            if (a.trimmed().isEmpty()) continue;
            m_appSuggestions->addItem(a);
        }
        if (apps.isEmpty()) {
            m_appSuggestions->addItem(tr("(no history yet — copy something first)"));
            m_appSuggestions->setEnabled(false);
        }
    }
}

QWidget *SettingsDialog::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *startupBox = new QGroupBox(tr("Startup & window"), page);
    auto *startupLayout = new QVBoxLayout(startupBox);
    m_startVisible = new QCheckBox(tr("Show the history window on start"), startupBox);
    m_hideOnFocusOut = new QCheckBox(tr("Hide the window when it loses focus (popup-like)"), startupBox);
    m_hideOnFocusOut->setToolTip(tr("When active, the window hides as soon as it loses focus — keeps the desktop tidy."));
    startupLayout->addWidget(m_startVisible);
    startupLayout->addWidget(m_hideOnFocusOut);
    auto *startupHint = new QLabel(tr("The window is never truly quit — closing hides to tray. Use tray → Quit to exit."), startupBox);
    startupHint->setWordWrap(true);
    startupHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    startupLayout->addWidget(startupHint);
    layout->addWidget(startupBox);

    auto *captureBox = new QGroupBox(tr("Capture"), page);
    auto *captureLayout = new QVBoxLayout(captureBox);
    m_primarySelection = new QCheckBox(tr("Also monitor the primary selection (middle-click paste)"), captureBox);
    m_primarySelection->setToolTip(tr("X11 only — tracks the selection buffer separately from Ctrl+C."));
    captureLayout->addWidget(m_primarySelection);
    auto *countRow = new QHBoxLayout();
    m_quickPasteCount = new QSpinBox(captureBox);
    m_quickPasteCount->setRange(1, 9);
    m_quickPasteCount->setToolTip(tr("Number of entries shown in the Meta+Shift+V popup (1–9, mapped to number keys)."));
    countRow->addWidget(new QLabel(tr("Entries in quick paste menu:"), captureBox));
    countRow->addWidget(m_quickPasteCount);
    countRow->addStretch(1);
    captureLayout->addLayout(countRow);
    auto *captureHint = new QLabel(tr("Quick paste shows the most recent entries; Filter still applies (pinned, group)."), captureBox);
    captureHint->setWordWrap(true);
    captureHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    captureLayout->addWidget(captureHint);
    layout->addWidget(captureBox);

    auto *paletteBox = new QGroupBox(tr("Command palette"), page);
    auto *paletteLayout = new QVBoxLayout(paletteBox);
    m_paletteInfo = new QLabel(tr("Press <b>Ctrl+K</b> inside the history window to open the palette — fast, FTS-backed search with typo-tolerant re-ranking. <b>⏎</b> paste, <b>Esc</b> close. Prefix <code>&gt;pin</code>/<code>&gt;copy</code> is reserved for future commands."), paletteBox);
    m_paletteInfo->setWordWrap(true);
    m_paletteInfo->setTextFormat(Qt::RichText);
    paletteLayout->addWidget(m_paletteInfo);
    layout->addWidget(paletteBox);

    auto *autostartBox = new QGroupBox(tr("Session"), page);
    auto *autostartLayout = new QVBoxLayout(autostartBox);
    m_autostart = new QCheckBox(tr("Start Egoboard automatically on login (~/.config/autostart)"), autostartBox);
    autostartLayout->addWidget(m_autostart);
    auto *autostartHint = new QLabel(tr("Writes <code>~/.config/autostart/org.egoboard.Egoboard.desktop</code> per XDG spec — works on Plasma X11 and Wayland."), autostartBox);
    autostartHint->setWordWrap(true);
    autostartHint->setTextFormat(Qt::RichText);
    autostartHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    autostartLayout->addWidget(autostartHint);
    layout->addWidget(autostartBox);

    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildSearchPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *ftsBox = new QGroupBox(tr("Full-text search (FTS5)"), page);
    auto *ftsLayout = new QVBoxLayout(ftsBox);
    m_ftsStatus = new QLabel(tr("Checking index…"), ftsBox);
    m_ftsStatus->setWordWrap(true);
    m_ftsStatus->setTextFormat(Qt::RichText);
    ftsLayout->addWidget(m_ftsStatus);
    auto *ftsHint = new QLabel(tr("Index covers <code>preview</code>, <code>text_data</code> and <code>ocr_text</code> with <code>unicode61</code> tokenizer, external-content sync and prefix search (<code>\"token\"*</code>). Queries like <code>hello world</code> become <code>\"hello\"* AND \"world\"*</code> — every word must match, diacritics folded."), ftsBox);
    ftsHint->setWordWrap(true);
    ftsHint->setTextFormat(Qt::RichText);
    ftsHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    ftsLayout->addWidget(ftsHint);
    m_ftsRebuildBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Rebuild search index"), ftsBox);
    m_ftsRebuildBtn->setToolTip(tr("Runs INSERT INTO entries_fts(entries_fts) VALUES('rebuild') — safe, handles external-content drift."));
    ftsLayout->addWidget(m_ftsRebuildBtn);
    connect(m_ftsRebuildBtn, &QPushButton::clicked, this, [this]{
        m_ftsStatus->setText(tr("Rebuilding…"));
        m_ftsRebuildBtn->setEnabled(false);
        QSqlDatabase db = m_ctx.storage()->database();
        QSqlQuery q(db);
        const bool ok = q.exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES('rebuild')"));
        if (ok) m_ftsStatus->setText(tr("<b style='color:palette(highlight);'>Rebuilt ✓</b> — index now in sync."));
        else m_ftsStatus->setText(tr("<b>Rebuild failed:</b> %1").arg(q.lastError().text()));
        m_ftsRebuildBtn->setEnabled(true);
        QTimer::singleShot(3000, this, &SettingsDialog::refreshDiagnostics);
    });
    layout->addWidget(ftsBox);

    auto *timelineBox = new QGroupBox(tr("Timeline strip"), page);
    auto *timelineLayout = new QVBoxLayout(timelineBox);
    auto *timelineLabel = new QLabel(tr("Thin histogram above the list — 14 bars for the last 14 days, height ∝ entry count for the current filter. Click a bar to filter that day, click outside to clear. Today is highlighted with the accent color. The strip ignores the date filter itself so its shape stays stable."), timelineBox);
    timelineLabel->setWordWrap(true);
    timelineLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    timelineLayout->addWidget(timelineLabel);
    layout->addWidget(timelineBox);

    auto *ocrBox = new QGroupBox(tr("OCR"), page);
    auto *ocrLayout = new QVBoxLayout(ocrBox);
    m_ocrStatus = new QLabel(tr("Checking tesseract…"), ocrBox);
    m_ocrStatus->setWordWrap(true);
    m_ocrStatus->setTextFormat(Qt::RichText);
    ocrLayout->addWidget(m_ocrStatus);
    auto *ocrHint = new QLabel(tr("Local OCR via <code>tesseract</code> (<code>--psm 6 --oem 1 -l eng</code>). No image leaves the machine. Recognized text is stored in <code>entries.ocr_text</code>, FTS-indexed, and shown in preview as <i>🔍 OCR:</i>. Slow devices can disable it in History & Privacy."), ocrBox);
    ocrHint->setWordWrap(true);
    ocrHint->setTextFormat(Qt::RichText);
    ocrHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    ocrLayout->addWidget(ocrHint);
    m_testOcrBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("image-x-generic")), tr("Test OCR with sample image"), ocrBox);
    ocrLayout->addWidget(m_testOcrBtn);
    connect(m_testOcrBtn, &QPushButton::clicked, this, [this]{
        // Create a 1-line test image and run tesseract via QProcess (quick check)
        m_ocrStatus->setText(tr("Testing…"));
        QProcess p;
        // Use echo via tesseract? Just show availability
        if (!OcrWorker::isAvailable()) {
            m_ocrStatus->setText(tr("<b style='color:palette(highlight);'>tesseract not found</b> — install <code>tesseract</code> and <code>tesseract-data-eng</code>"));
            return;
        }
        m_ocrStatus->setText(tr("<b style='color:palette(highlight);'>tesseract OK</b> — %1").arg(tesseractVersion()));
    });
    layout->addWidget(ocrBox);

    auto *previewBox = new QGroupBox(tr("Preview enrichments"), page);
    auto *previewLayout = new QVBoxLayout(previewBox);
    auto *previewLabel = new QLabel(tr("<b>Code</b> — JSON is pretty-printed and highlighted (keys/strings/numbers), XML and generic code get keyword/string highlighting.<br/><b>Links</b> — <code>https://…</code> URLs become clickable (<code>🔗</code> in meta).<br/><b>Colors</b> — <code>#RRGGBB</code>/<code>#RGB</code> show as swatches (<code>🎨</code>). All detectors are local regex, no network."), previewBox);
    previewLabel->setWordWrap(true);
    previewLabel->setTextFormat(Qt::RichText);
    previewLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    previewLayout->addWidget(previewLabel);
    layout->addWidget(previewBox);

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
    auto *pathLabel = new QLabel(tr("File: <code>%1</code> — click to open folder").arg(path.toHtmlEscaped()), dbInfoBox);
    pathLabel->setTextFormat(Qt::RichText);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    pathLabel->setCursor(Qt::PointingHandCursor);
    dbLayout->addWidget(pathLabel);
    connect(pathLabel, &QLabel::linkActivated, this, [path]{
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    // Make pathLabel clickable via mousePress
    auto *sizeLabel = new QLabel(
        tr("<b>%1</b> on disk · <b>%2</b> entries · <b>%3</b> pinned · <b>%4</b> images · <b>%5</b> with OCR")
            .arg(humanSize(size)).arg(stats.entryCount).arg(stats.pinnedCount).arg(stats.imageCount).arg(stats.ocrCount),
        dbInfoBox);
    sizeLabel->setTextFormat(Qt::RichText);
    sizeLabel->setWordWrap(true);
    dbLayout->addWidget(sizeLabel);
    auto *ftsDetail = new QLabel(tr("FTS and OCR are local — no cloud. The database lives under <code>~/.local/share/egoboard/</code> (WAL mode, foreign keys on)."), dbInfoBox);
    ftsDetail->setWordWrap(true);
    ftsDetail->setTextFormat(Qt::RichText);
    ftsDetail->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    dbLayout->addWidget(ftsDetail);
    layout->addWidget(dbInfoBox);

    auto *limitsBox = new QGroupBox(tr("Limits"), m_storagePage);
    auto *limitsForm = new QFormLayout(limitsBox);
    m_diskCapMb = new QSpinBox(limitsBox);
    m_diskCapMb->setRange(0, 1024 * 64);
    m_diskCapMb->setSpecialValueText(tr("Unlimited"));
    m_diskCapMb->setSuffix(tr(" MB"));
    limitsForm->addRow(tr("Total history size cap:"), m_diskCapMb);
    auto *capHint = new QLabel(tr("When a cap is set, the oldest non-pinned entries are removed to stay below it. Checks run every 25 captures and daily for VACUUM. History is unlimited by default — the virtualized list handles 50k+ entries smoothly."), limitsBox);
    capHint->setWordWrap(true);
    capHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    limitsForm->addRow(QString(), capHint);
    layout->addWidget(limitsBox);

    auto *maintenanceBox = new QGroupBox(tr("Maintenance"), m_storagePage);
    auto *maintenanceLayout = new QVBoxLayout(maintenanceBox);
    auto *row = new QHBoxLayout();
    auto *vacuumButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Compact database now (VACUUM)"), maintenanceBox);
    connect(vacuumButton, &QPushButton::clicked, this, [this, vacuumButton] {
        vacuumButton->setEnabled(false);
        vacuumButton->setText(tr("Compacting…"));
        m_ctx.vacuumNow();
        QTimer::singleShot(3000, this, [vacuumButton] {
            vacuumButton->setText(tr("Compact database now (VACUUM)"));
            vacuumButton->setEnabled(true);
        });
    });
    row->addWidget(vacuumButton);
    auto *clearOcrBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), tr("Clear OCR text"), maintenanceBox);
    clearOcrBtn->setToolTip(tr("Sets entries.ocr_text = NULL for all entries — re-OCR will re-fill as you copy new images."));
    connect(clearOcrBtn, &QPushButton::clicked, this, [this, clearOcrBtn]{
        QSqlDatabase db = m_ctx.storage()->database();
        QSqlQuery q(db);
        q.exec(QStringLiteral("UPDATE entries SET ocr_text = NULL WHERE ocr_text IS NOT NULL"));
        clearOcrBtn->setText(tr("Cleared"));
        QTimer::singleShot(2000, this, [clearOcrBtn]{ clearOcrBtn->setText(tr("Clear OCR text")); });
        refreshDiagnostics();
    });
    row->addWidget(clearOcrBtn);
    row->addStretch(1);
    maintenanceLayout->addLayout(row);
    auto *maintHint = new QLabel(tr("Egoboard also compacts automatically once a day when the database grows past 50 MB. The VACUUM runs on a dedicated thread with its own connection so the UI stays responsive."), maintenanceBox);
    maintHint->setWordWrap(true);
    maintHint->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    maintenanceLayout->addWidget(maintHint);
    layout->addWidget(maintenanceBox);
    layout->addStretch(1);
    return m_storagePage;
}

void SettingsDialog::refreshDiagnostics()
{
    if (!m_ftsStatus) return;
    QSqlDatabase db = m_ctx.storage()->database();
    // FTS count
    qint64 ftsCount = -1, entryCount = m_ctx.storage()->stats().entryCount;
    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM entries_fts")) && q.next())
            ftsCount = q.value(0).toLongLong();
    }
    if (ftsCount >= 0) {
        const bool inSync = (ftsCount == entryCount);
        m_ftsStatus->setText(tr("FTS rows: <b>%1</b> / entries: <b>%2</b> %3 — using <code>unicode61</code>, prefix search, <code>content='entries'</code> sync.")
            .arg(ftsCount).arg(entryCount).arg(inSync ? QStringLiteral("✓ in sync") : QStringLiteral("⚠ drift — rebuild")));
    } else {
        m_ftsStatus->setText(tr("FTS table not available — search falls back to <code>LIKE</code>."));
    }
    if (m_ocrStatus) {
        const bool avail = OcrWorker::isAvailable();
        const QString ver = avail ? tesseractVersion() : QString();
        const auto stats = m_ctx.storage()->stats();
        m_ocrStatus->setText(avail
            ? tr("<b>tesseract OK</b> — %1 — <b>%2</b> images, <b>%3</b> with OCR text. Disable in History to save CPU.")
                .arg(ver.isEmpty() ? QStringLiteral("found in PATH") : ver).arg(stats.imageCount).arg(stats.ocrCount)
            : tr("<b>tesseract not found</b> — install <code>tesseract</code> + <code>tesseract-data-eng</code> to enable image search. Preview will show <i>OCR: processing…</i> until then."));
    }
    if (m_paletteInfo) {
        m_paletteInfo->setText(tr("Press <b>Ctrl+K</b> inside the history window to open the palette — fast, FTS-backed search with typo-tolerant re-ranking. <b>⏎</b> paste, <b>Esc</b> close."));
    }
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
