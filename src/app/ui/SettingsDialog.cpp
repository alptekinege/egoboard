#include "SettingsDialog.h"

#include "../ApplicationContext.h"
#include "../BackupService.h"
#include "../ColorSchemeIndex.h"
#include "../HotkeyManager.h"
#include "../IconThemeIndex.h"
#include "../LayerShellHelper.h"
#include "../WlrDataControlHelper.h"
#include "../EncryptionManager.h"
#include "../ScriptActionManager.h"
#include "../SettingsManager.h"
#include "../OcrWorker.h"
#include "../PortalPaster.h"
#include "AppearancePreview.h"
#include "DashboardDialog.h"
#include "DesignTokens.h"
#include "SettingsStructure.h"
#include "SnippetManager.h"
#include "StorageManager.h"
#include "TransformEngine.h"
#include "UiHelpers.h"
#include "../../core/SensitiveDataDetector.h"
#include "../../core/ExpirePolicy.h"
#include "ExportImportDialogs.h"
#include "ExportImportManager.h"
#include "SearchEngine.h"

#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QApplication>
#include <QAbstractButton>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPointer>

#include <KColorButton>
#include <KGlobalAccel>
#include <KKeySequenceWidget>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <atomic>
#include <limits>

namespace {
// The dialog's hints and byte sizes are the app-wide ones (UiHelpers), so the
// settings pages cannot drift from the list and the preview.
using UiHelpers::humanSize;
using UiHelpers::makeHint;
using UiHelpers::makeStatusPanel;

// Wide-mode icon sidebar width: SettingsStructure::kSidebarWideWidth, shared
// by the factory clamp and the responsive restore path below.

// Every page scrolls the same way, whatever its content height.
QWidget *makeScrollable(QWidget *page)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QString tesseractVersion()
{
    QProcess p;
    p.start(QStringLiteral("tesseract"), {QStringLiteral("--version")});
    if (!p.waitForFinished(1200)) return {};
    QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
    const QString first = out.split(QLatin1Char('\n')).value(0).trimmed();
    return first.isEmpty() ? QStringLiteral("tesseract") : first;
}

QString kwinVersion()
{
    QProcess p;
    p.start(QStringLiteral("kwin_wayland"), {QStringLiteral("--version")});
    if (!p.waitForFinished(800)) {
        p.start(QStringLiteral("kwin_x11"), {QStringLiteral("--version")});
        if (!p.waitForFinished(800)) return {};
    }
    QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
    return out.split(QLatin1Char('\n')).value(0).trimmed();
}

// Runs fn on a worker thread and shows a non-cancelable progress dialog until
// it finishes. The return type must be copyable.
template<typename Fn>
auto runWithProgress(QWidget *parent, const QString &label, Fn &&fn)
    -> decltype(fn())
{
    QProgressDialog dialog(label, QString(), 0, 0, parent);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(0);
    dialog.setCancelButton(nullptr);
    
    using ResultType = decltype(fn());
    QFutureWatcher<ResultType> watcher(parent);
    QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &dialog, &QProgressDialog::close);
    
    auto future = QtConcurrent::run(std::forward<Fn>(fn));
    watcher.setFuture(future);
    
    dialog.exec();
    return future.result();
}

// U11 (G9): chunked synchronous run on the GUI thread for cooperative
// cancel/progress ops (export/import/Klipper): fn takes
// (std::atomic<bool> *, IoProgress). Same thread as the storage connection,
// so — unlike runWithProgress — no worker-thread SQL; the progress callback
// pumps the event loop between batches so Cancel takes effect promptly.
template<typename Fn>
auto runIoWithProgress(QWidget *parent, const QString &label, Fn &&fn)
    -> decltype(fn(nullptr, ExportImportManager::IoProgress{}))
{
    std::atomic<bool> cancel{false};
    QProgressDialog dialog(label, QObject::tr("Cancel"), 0, 0, parent);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(0);
    QObject::connect(&dialog, &QProgressDialog::canceled, &dialog, [&cancel] {
        cancel.store(true, std::memory_order_relaxed);
    });
    dialog.show();
    auto result = fn(&cancel, [&](int done, int total) {
        if (total > 0) {
            dialog.setMaximum(total);
            dialog.setValue(done);
        }
        dialog.setLabelText(
            QObject::tr("%1 %2 of %3 entries").arg(label).arg(done).arg(total));
        QApplication::processEvents();
    });
    dialog.close();
    return result;
}
} // namespace

SettingsDialog::SettingsDialog(ApplicationContext &context, QWidget *parent)
    : QDialog(parent)
    , m_ctx(context)
{
    setWindowTitle(tr("Egoboard Settings"));
    setModal(true);
    resize(800, 640);
    setMinimumSize(560, 420); // narrow windows compress instead of clipping (R1)

    auto *layout = new QVBoxLayout(this);

    // U14 search box: filters the sidebar pages below; matching knobs on the
    // visible page are bolded. Same search-field look as the main window.
    auto *searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search settings…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Search settings"));
    m_search->setAccessibleDescription(
        tr("Filters the settings pages; matching options are shown in bold."));
    UiHelpers::styleSearchField(m_search);
    searchRow->addWidget(m_search, 1);
    m_searchCount = UiHelpers::makeHint(QString(), this, /*richText=*/false);
    searchRow->addWidget(m_searchCount);
    layout->addLayout(searchRow);
    connect(m_search, &QLineEdit::textChanged, this, &SettingsDialog::applySettingsSearch);

    // Sidebar + page stack: icon-on-top, label-below items stacked vertically
    // (settings sidebar style) instead of a rotated west tab column.
    // m_content flips to a column with a horizontal top strip under the
    // collapse token (U14 responsive narrow layout).
    auto *content = m_content = new QHBoxLayout();
    content->setContentsMargins(0, 0, 0, 0);
    auto *sidebar = m_sidebar = SettingsStructure::createSidebar(this);

    auto *stack = m_stack = new QStackedWidget(this);
    // Sidebar plan (SettingsStructure): Normal pages first, the "Advanced"
    // group header, advanced pages, About last. Sidebar rows and stack pages
    // no longer share indices — m_sidebarToStack maps them (headers map to
    // -1 and are never selectable, so they can never become current).
    QVector<QWidget *> pages;
    pages.reserve(11);
    pages << buildGeneralPage() << buildCapturePage() << buildHistoryPage() << buildUsagePage()
          << buildHotkeysPage() << buildStoragePage() << buildPrivacyPage()
          << buildSearchPreviewPage() << buildAutomationPage() << buildPlatformDiagnosticsPage()
          << buildAboutPage();
    m_sidebarToStack.clear();
    const QVector<SettingsStructure::SidebarRow> plan = SettingsStructure::sidebarRows();
    SettingsStructure::populateSidebar(sidebar); // centered rows in plan order
    Q_ASSERT(plan.size() == m_sidebar->count());
    for (const SettingsStructure::SidebarRow &row : plan) {
        if (row.header) {
            m_sidebarToStack.append(-1); // group header: no page, never current
            continue;
        }
        if (row.page < 0 || row.page >= pages.size())
            continue; // programming error: plan and builders diverged (pinned by tests)
        m_stack->addWidget(pages.at(row.page));
        m_sidebarToStack.append(m_stack->count() - 1);
    }
    connect(sidebar, &QListWidget::currentRowChanged, this, [this](int sidebarRow) {
        const int stackIndex = m_sidebarToStack.value(sidebarRow, -1);
        if (stackIndex >= 0)
            m_stack->setCurrentIndex(stackIndex);
    });
    // The Usage page is a live snapshot: re-collect on every visit, since
    // captures may have landed while the dialog stayed open.
    if (m_usagePanel) {
        const int usageStack = m_stack->indexOf(m_usagePanel);
        const int usageRow = m_sidebarToStack.indexOf(usageStack);
        connect(sidebar, &QListWidget::currentRowChanged, this,
                [this, usageRow](int row) {
                    if (row == usageRow)
                        m_usagePanel->refresh();
                });
    }
    sidebar->setCurrentRow(0);

    content->addWidget(sidebar);
    content->addWidget(stack, 1);
    layout->addLayout(content, 1);
    applyResponsiveLayout(); // honor the current width on first show (U14 narrow)

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this,
            [this] { save(); accept(); });
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    load();
    // Live preview for the appearance combos; connected after load() so filling
    // them does not fire a preview. Cancel restores the stored combination.
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int) { previewThemes(); });
    connect(m_iconThemeCombo, &QComboBox::currentIndexChanged, this, [this](int) { previewThemes(); });
    connect(this, &QDialog::rejected, this, [this] {
        // Cancel: put the stored theme pair back (the text controls save as they
        // are changed, so only the combos need restoring).
        m_ctx.applyThemes(m_ctx.settings()->theme(), m_ctx.settings()->iconTheme(),
                          m_ctx.settings()->textAppearance(), /*force=*/true);
    });
    // Defer heavy work — constructor must not block UI (tesseract/kwin probes + DB)
    QTimer::singleShot(0, this, &SettingsDialog::refreshDiagnostics);
    // Populate dynamic lists after load (also deferred to keep open instant)
    QTimer::singleShot(0, this, [this]{
        populateTransformList();
        populateSnippetList();
        populateScriptList();
        // Late-arriving list rows are searchable too: re-run an active filter.
        if (m_search && !m_search->text().trimmed().isEmpty())
            applySettingsSearch();
    });
    // App suggestions — deferred so open stays instant, but synchronous on the
    // GUI thread: StorageManager's connection is owned by the GUI thread and
    // must never be touched from a worker (U19). The DISTINCT scan rides
    // idx_entries_app, so it stays cheap even at 50k entries.
    QTimer::singleShot(0, this, [this]{
        if (!m_appSuggestions) return;
        const QStringList apps = m_ctx.storage()->sourceApps();
        m_appSuggestions->clear();
        if (apps.isEmpty()) {
            m_appSuggestions->addItem(tr("(no history yet — copy something first)"));
            m_appSuggestions->setEnabled(false);
        } else {
            for (const QString &a : apps) {
                if (a.trimmed().isEmpty()) continue;
                m_appSuggestions->addItem(a);
            }
            m_appSuggestions->setEnabled(true);
        }
    });
}

// --- General (startup, tray, appearance) -------------------------------------
QWidget *SettingsDialog::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *startupBox = new QGroupBox(tr("Startup & window"), page);
    auto *startupLayout = new QVBoxLayout(startupBox);
    m_startVisible = new QCheckBox(tr("Show the history window on start"), startupBox);
    m_hideOnFocusOut = new QCheckBox(tr("Hide the window when it loses focus (popup-like)"), startupBox);
    m_hideOnFocusOut->setToolTip(tr("When active, the window hides as soon as it loses focus — keeps the desktop tidy."));
    m_autostart = new QCheckBox(tr("Start Egoboard automatically on login (~/.config/autostart)"), startupBox);
    m_autostart->setToolTip(tr("Writes <code>Exec=\"%1\"</code> — a bare command name is not resolvable for the systemd autostart generator Plasma 6 uses, which then skips the entry silently.").arg(SettingsManager::autostartExecutablePath().toHtmlEscaped()));
    m_rememberGeometry = new QCheckBox(tr("Remember window size, position and splitter"), startupBox);
    m_restoreFilter = new QCheckBox(tr("Restore the last filter on start"), startupBox);
    startupLayout->addWidget(m_startVisible);
    startupLayout->addWidget(m_hideOnFocusOut);
    startupLayout->addWidget(m_autostart);
    // Which executable the login entry runs: defaults to the running binary,
    // but an .AppImage (or an installed copy) can be chosen instead. The entry is
    // rewritten whenever this changes and on every start, so it cannot go stale.
    auto *autostartRow = new QWidget(startupBox);
    auto *autostartRowLayout = new QHBoxLayout(autostartRow);
    autostartRowLayout->setContentsMargins(0, 0, 0, 0);
    m_autostartCommand = new QLineEdit(autostartRow);
    m_autostartCommand->setReadOnly(true);
    m_autostartCommand->setToolTip(tr("Absolute path the login entry launches."));
    auto *chooseAutostart = new QPushButton(tr("Choose…"), autostartRow);
    chooseAutostart->setToolTip(tr("Pick the .AppImage (or an installed binary) to always start that copy at login."));
    auto *resetAutostart = new QPushButton(tr("Use running binary"), autostartRow);
    resetAutostart->setToolTip(tr("Forget the choice and start whatever binary is running."));
    autostartRowLayout->addWidget(m_autostartCommand, 1);
    autostartRowLayout->addWidget(chooseAutostart);
    autostartRowLayout->addWidget(resetAutostart);
    startupLayout->addWidget(autostartRow);

    const auto refreshAutostartCommand = [this] {
        if (m_autostartCommand)
            m_autostartCommand->setText(m_ctx.settings()->effectiveAutostartCommand());
    };
    connect(chooseAutostart, &QPushButton::clicked, this, [this, refreshAutostartCommand] {
        const QString chosen = m_ctx.settings()->autostartCommand();
        const QString start = chosen.isEmpty()
            ? QFileInfo(SettingsManager::autostartExecutablePath()).absolutePath()
            : chosen;
        const QString path = QFileDialog::getOpenFileName(this, tr("Autostart command"), start);
        if (path.isEmpty())
            return;
        m_ctx.settings()->setAutostartCommand(path);
        refreshAutostartCommand();
    });
    connect(resetAutostart, &QPushButton::clicked, this, [this, refreshAutostartCommand] {
        m_ctx.settings()->setAutostartCommand(QString());
        refreshAutostartCommand();
    });
    connect(m_autostart, &QCheckBox::toggled, autostartRow,
            [autostartRow](bool on) { autostartRow->setEnabled(on); });
    startupLayout->addWidget(m_rememberGeometry);
    startupLayout->addWidget(m_restoreFilter);
    startupLayout->addWidget(makeHint(tr("The window is never truly quit — closing hides to tray. Use tray → Quit to exit. Autostart writes <code>~/.config/autostart/org.egoboard.Egoboard.desktop</code> with an absolute <code>Exec=</code>, rewritten on every start — Plasma 6 turns these files into systemd units and silently skips entries whose command cannot be resolved. Choose the .AppImage above to always start that copy at login."), startupBox));
    layout->addWidget(startupBox);

    auto *trayBox = new QGroupBox(tr("Tray & notifications"), page);
    auto *trayLayout = new QFormLayout(trayBox);
    trayLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_trayMode = new QComboBox(trayBox);
    m_trayMode->addItem(tr("Auto (show when history not empty)"), QStringLiteral("auto"));
    m_trayMode->addItem(tr("Always show"), QStringLiteral("always"));
    m_trayMode->addItem(tr("Hidden (no tray icon)"), QStringLiteral("hidden"));
    trayLayout->addRow(tr("Tray icon:"), m_trayMode);

    // What the clicks do. Both combos share the same options; the wheel below
    // walks the recent entries like Klipper does.
    const auto addClickOptions = [this](QComboBox *combo) {
        combo->addItem(tr("Show/hide history"), int(SettingsManager::TrayClick::ShowWindow));
        combo->addItem(tr("Quick paste menu"), int(SettingsManager::TrayClick::QuickPaste));
        combo->addItem(tr("Pause/resume capture"), int(SettingsManager::TrayClick::TogglePause));
        combo->addItem(tr("Do nothing"), int(SettingsManager::TrayClick::Nothing));
    };
    m_trayPrimaryClick = new QComboBox(trayBox);
    addClickOptions(m_trayPrimaryClick);
    m_trayPrimaryClick->setToolTip(tr("Middle click is the secondary button; left click is the usual activation."));
    trayLayout->addRow(tr("Left click:"), m_trayPrimaryClick);
    m_traySecondaryClick = new QComboBox(trayBox);
    addClickOptions(m_traySecondaryClick);
    trayLayout->addRow(tr("Middle click:"), m_traySecondaryClick);
    m_trayWheelCycles = new QCheckBox(tr("Scroll wheel walks the recent entries"), trayBox);
    m_trayWheelCycles->setToolTip(tr("Wheel up copies the next older entry to the clipboard, wheel down comes back. A new copy starts over."));
    trayLayout->addRow(QString(), m_trayWheelCycles);

    m_notifications = new QCheckBox(tr("Show notification when sensitive content is skipped"), trayBox);
    trayLayout->addRow(QString(), m_notifications);

    m_captureSound = new QCheckBox(tr("Play a sound on new copy"), trayBox);
    m_captureSound->setToolTip(tr("A short beep when a new clipboard entry is captured (not when the same content is already at the top)."));
    trayLayout->addRow(QString(), m_captureSound);

    m_captureNotification = new QCheckBox(tr("Show a notification on new copy"), trayBox);
    m_captureNotification->setToolTip(tr("Shows the source app and a preview of the new clipboard entry. Requires the global notifications setting to be on."));
    trayLayout->addRow(QString(), m_captureNotification);

    trayLayout->addRow(QString(), makeHint(tr("Tray uses <code>KStatusNotifierItem</code> (Plasma). Hidden still keeps the app running — show via hotkey."), trayBox));
    layout->addWidget(trayBox);

    auto *appearanceBox = new QGroupBox(tr("Appearance"), page);
    auto *appearanceLayout = new QVBoxLayout(appearanceBox);
    auto *appearanceForm = new QFormLayout();
    appearanceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_themeCombo = new QComboBox(appearanceBox);
    m_themeCombo->addItem(tr("System (follow desktop)"), QStringLiteral("system"));
    // One entry per installed KDE color scheme, discovered from the same XDG
    // directories Plasma reads — schemes the user installs later show up here
    // without any change to this dialog.
    const QVector<ColorSchemeIndex::Entry> schemes = ColorSchemeIndex::scan();
    for (const ColorSchemeIndex::Entry &scheme : schemes)
        m_themeCombo->addItem(scheme.name, scheme.id);
    appearanceForm->addRow(tr("Color theme:"), m_themeCombo);
    m_iconThemeCombo = new QComboBox(appearanceBox);
    m_iconThemeCombo->addItem(tr("System (follow desktop)"), QStringLiteral("system"));
    // Same discovery as the color themes: every icon theme installed on the
    // machine (system-wide or per-user) can be picked, no code change needed.
    const QVector<IconThemeIndex::Entry> iconThemes = IconThemeIndex::scan();
    for (const IconThemeIndex::Entry &iconTheme : iconThemes)
        m_iconThemeCombo->addItem(iconTheme.name, iconTheme.id);
    m_iconThemeCombo->setToolTip(tr("Icons are drawn from this KDE icon theme — toolbar, menus, list entries and dialogs all follow it."));
    appearanceForm->addRow(tr("Icon theme:"), m_iconThemeCombo);

    // Text readability: size and color knobs for schemes whose text is hard to
    // read. These three save as you change them (the preview below updates with
    // the palette), unlike the theme combos, which preview until OK/Apply.
    m_fontSize = new QSpinBox(appearanceBox);
    m_fontSize->setRange(-2, 6);
    m_fontSize->setSuffix(tr(" pt"));
    m_fontSize->setSpecialValueText(tr("Default"));
    m_fontSize->setToolTip(tr("Shifts the whole UI font size — useful when a theme renders text too small to read."));
    appearanceForm->addRow(tr("Text size:"), m_fontSize);

    const auto makeColorRow = [&](QComboBox **combo, KColorButton **button, const QString &tooltip) {
        auto *row = new QWidget(appearanceBox);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        *combo = new QComboBox(row);
        (*combo)->addItem(tr("Theme (contrast-checked)"), QStringLiteral("theme"));
        (*combo)->addItem(tr("Custom…"), QStringLiteral("custom"));
        (*combo)->setToolTip(tooltip);
        *button = new KColorButton(row);
        (*button)->setToolTip(tooltip);
        layout->addWidget(*combo, 1);
        layout->addWidget(*button);
        return row;
    };
    appearanceForm->addRow(tr("Text color:"), makeColorRow(&m_textColorCombo, &m_textColorButton,
        tr("Main text in lists, labels and buttons. \"Theme\" keeps the scheme's color but raises it until it is readable.")));
    appearanceForm->addRow(tr("Secondary text:"), makeColorRow(&m_dimTextColorCombo, &m_dimTextColorButton,
        tr("Timestamps, source apps, hints and placeholders — the small print.")));

    connect(m_fontSize, &QSpinBox::valueChanged, this, [this](int delta) {
        m_ctx.settings()->setFontPointDelta(delta);
    });
    connect(m_textColorCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool custom = m_textColorCombo->currentData().toString() == QLatin1String("custom");
        m_textColorButton->setEnabled(custom);
        m_ctx.settings()->setTextColor(custom ? m_textColorButton->color().name() : QString());
    });
    connect(m_textColorButton, &KColorButton::changed, this, [this](const QColor &color) {
        if (m_textColorCombo->currentData().toString() == QLatin1String("custom"))
            m_ctx.settings()->setTextColor(color.name());
    });
    connect(m_dimTextColorCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool custom = m_dimTextColorCombo->currentData().toString() == QLatin1String("custom");
        m_dimTextColorButton->setEnabled(custom);
        m_ctx.settings()->setDimTextColor(custom ? m_dimTextColorButton->color().name() : QString());
    });
    connect(m_dimTextColorButton, &KColorButton::changed, this, [this](const QColor &color) {
        if (m_dimTextColorCombo->currentData().toString() == QLatin1String("custom"))
            m_ctx.settings()->setDimTextColor(color.name());
    });

    appearanceLayout->addWidget(makeHint(
        tr("Preview — the rows, hint and placeholder text as the app draws them:"), appearanceBox));
    appearanceLayout->addWidget(new AppearancePreview(appearanceBox));
    m_densityCombo = new QComboBox(appearanceBox);
    m_densityCombo->addItem(tr("Compact"), QStringLiteral("compact"));
    m_densityCombo->addItem(tr("Comfortable"), QStringLiteral("comfortable"));
    m_densityCombo->addItem(tr("Spacious"), QStringLiteral("spacious"));
    m_densityCombo->setToolTip(tr("Vertical breathing room of the history list rows."));
    appearanceForm->addRow(tr("List density:"), m_densityCombo);
    m_timestampCombo = new QComboBox(appearanceBox);
    m_timestampCombo->addItem(tr("Relative (2 h ago)"), QStringLiteral("relative"));
    m_timestampCombo->addItem(tr("Absolute (2026-09-09 14:30)"), QStringLiteral("absolute"));
    appearanceForm->addRow(tr("Timestamps:"), m_timestampCombo);
    m_clock24h = new QCheckBox(tr("Use 24-hour clock in timestamps"), appearanceBox);
    appearanceLayout->addWidget(m_clock24h);
    appearanceLayout->addLayout(appearanceForm);
    m_toolbarIconOnly = new QCheckBox(tr("Show toolbar buttons as icons only (compact)"), appearanceBox);
    m_toolbarIconOnly->setToolTip(tr("Toolbar buttons appear as logos only — hover for the label. Text+icon otherwise. Takes effect immediately, also while the window is open."));
    appearanceLayout->addWidget(m_toolbarIconOnly);
    m_reduceMotion = new QCheckBox(tr("Reduce motion (no popup fades or timeline hover transition)"), appearanceBox);
    m_reduceMotion->setToolTip(tr("Animations are capped at ~120 ms; turning this on removes them entirely."));
    appearanceLayout->addWidget(m_reduceMotion);
    m_groupByDay = new QCheckBox(tr("Group history list by day (sticky date headers)"), appearanceBox);
    m_groupByDay->setToolTip(tr("Shows one header row per day above the entries. Off = plain chronological list."));
    appearanceLayout->addWidget(m_groupByDay);
    m_showEntryIndex = new QCheckBox(tr("Show entry index in rows (#1, #2, …)"), appearanceBox);
    appearanceLayout->addWidget(m_showEntryIndex);
    m_showUseCountBadge = new QCheckBox(tr("Show use-count badge in rows (0 pastes)"), appearanceBox);
    appearanceLayout->addWidget(m_showUseCountBadge);
    m_privacyBlur = new QCheckBox(tr("Blur sensitive previews until hovered"), appearanceBox);
    m_privacyBlur->setToolTip(tr("Sensitive entries stay blurred in the preview until hovered or focused."));
    appearanceLayout->addWidget(m_privacyBlur);
    appearanceLayout->addWidget(makeHint(tr("Color themes are the KDE color schemes (<code>color-schemes</code> dirs) and icon themes installed on this system; System follows whichever Plasma has active. Themes preview live and apply on OK/Apply; text size and colors take effect as you change them. Text colors marked \"contrast-checked\" keep the scheme's color but raise it until it is readable."), appearanceBox));
    layout->addWidget(appearanceBox);

    auto *pastingBox = new QGroupBox(tr("Pasting"), page);
    auto *pastingLayout = new QVBoxLayout(pastingBox);
    m_closeAfterPaste = new QCheckBox(tr("Close the Egoboard window after pasting"), pastingBox);
    m_closeAfterPaste->setToolTip(tr("When on, the window hides as soon as you activate an entry so focus returns to the app you paste into."));
    pastingLayout->addWidget(m_closeAfterPaste);
    m_bumpOnPaste = new QCheckBox(tr("Move the pasted entry to the top of history"), pastingBox);
    m_bumpOnPaste->setToolTip(tr("Pasting an older entry bumps its timestamp and use count, so it shows up first."));
    pastingLayout->addWidget(m_bumpOnPaste);
    m_pasteAsPlainText = new QCheckBox(tr("Always paste as plain text (strip formatting)"), pastingBox);
    pastingLayout->addWidget(m_pasteAsPlainText);
    pastingLayout->addWidget(makeHint(tr("Plain-text paste affects rich text (HTML) entries — images and file copies are unchanged. The stored entry keeps its original formatting either way."), pastingBox));
    // R6 portal paste (Wayland only, opt-in): the compositor presses Ctrl+V
    // through the desktop portal after its own permission prompt.
    m_portalPaste = new QCheckBox(tr("Paste via the Wayland portal (asks permission first)"), pastingBox);
    m_portalPaste->setToolTip(tr("On Wayland, asks the compositor to press Ctrl+V through the desktop portal instead of showing the manual-paste notification. Off everywhere else."));
    pastingLayout->addWidget(m_portalPaste);
    m_portalStatus = makeStatusPanel(QString(), pastingBox);
    pastingLayout->addWidget(m_portalStatus);
    pastingLayout->addWidget(makeHint(tr("How it works: on the first paste of each session Plasma shows a permission prompt from the compositor — nothing is pasted without that approval, and clipboard contents never leave the machine. Turn this off anytime to go back to the manual Ctrl+V notification; the fallback always stays. Needs a portal-providing compositor (Plasma Wayland); without one the notification path is used automatically."), pastingBox));
    layout->addWidget(pastingBox);

    auto *generalResetRow = new QHBoxLayout();
    auto *generalResetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                            tr("Reset this page to defaults"), page);
    generalResetBtn->setToolTip(tr("Restores startup, tray, appearance and pasting options to their defaults. History data is kept."));
    generalResetBtn->setAccessibleName(tr("Reset General page to defaults"));
    generalResetBtn->setAccessibleDescription(
        tr("Restores startup, tray, appearance and pasting options to their defaults."));
    connect(generalResetBtn, &QPushButton::clicked, this, [this] {
        resetPageToDefaults(SettingsManager::SettingsPage::General);
    });
    generalResetRow->addWidget(generalResetBtn);
    generalResetRow->addStretch(1);
    layout->addLayout(generalResetRow);

    layout->addStretch(1);
    return makeScrollable(page);
}

// --- Capture (what gets recorded, limits, per-app rules) ---------------------
QWidget *SettingsDialog::buildCapturePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *recordBox = new QGroupBox(tr("What to record"), page);
    auto *recordLayout = new QVBoxLayout(recordBox);
    m_primarySelection = new QCheckBox(tr("Also monitor the primary selection (middle-click paste)"), recordBox);
    m_primarySelection->setToolTip(tr("X11 only — tracks the selection buffer separately from Ctrl+C. On Wayland it also enables data-control primary selection."));
    recordLayout->addWidget(m_primarySelection);
    // Order matters: load()/save() map these four by index to
    // Text / RichText / Image / Files capture switches.
    const QStringList typeLabels = {
        tr("Plain text"),
        tr("Rich text (HTML)"),
        tr("Images"),
        tr("File copies"),
    };
    for (const QString &label : typeLabels) {
        auto *box = new QCheckBox(label, recordBox);
        recordLayout->addWidget(box);
        m_captureTypeBoxes.append(box);
    }
    recordLayout->addWidget(makeHint(tr("Unticked types are skipped at capture time — they never reach the database. Existing entries are kept."), recordBox));
    m_pauseOnLock = new QCheckBox(tr("Pause recording while the session is locked"), recordBox);
    m_pauseOnLock->setToolTip(tr("Locking the screen (or switching users) stops capture until the session is unlocked. Manual pause is in the tray menu and on a global shortcut."));
    recordLayout->addWidget(m_pauseOnLock);
    layout->addWidget(recordBox);

    auto *limitsBox = new QGroupBox(tr("Limits"), page);
    auto *limitsForm = new QFormLayout(limitsBox);
    limitsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_debounce = new QSpinBox(limitsBox);
    m_debounce->setRange(50, 5000);
    m_debounce->setSingleStep(50);
    m_debounce->setSuffix(tr(" ms"));
    limitsForm->addRow(tr("Debounce interval:"), m_debounce);
    limitsForm->addRow(QString(), makeHint(tr("Coalesces rapid clipboard updates (apps that set several MIME types). Lower is more responsive, higher avoids duplicates. Wave: 50 ms≈instant, 250 ms≈balanced, 1000 ms≈conservative."), limitsBox));
    m_maxItemMb = new QSpinBox(limitsBox);
    m_maxItemMb->setRange(0, 512);
    m_maxItemMb->setSpecialValueText(tr("No limit"));
    m_maxItemMb->setSuffix(tr(" MB"));
    limitsForm->addRow(tr("Max size per text entry:"), m_maxItemMb);
    m_maxImageMb = new QSpinBox(limitsBox);
    m_maxImageMb->setRange(0, 512);
    m_maxImageMb->setSpecialValueText(tr("No limit (use text cap)"));
    m_maxImageMb->setSuffix(tr(" MB"));
    limitsForm->addRow(tr("Max size per image:"), m_maxImageMb);
    limitsForm->addRow(QString(), makeHint(tr("Oversized images are not stored (placeholder only); oversized text is truncated with … . Default 5 MB text / 8 MB image — keeps DB fast. 0 = no limit (not recommended)."), limitsBox));
    auto *countRow = new QHBoxLayout();
    m_quickPasteCount = new QSpinBox(limitsBox);
    m_quickPasteCount->setRange(1, 9);
    m_quickPasteCount->setToolTip(tr("Number of entries shown in the Meta+V popup (1–9, mapped to number keys)."));
    countRow->addWidget(m_quickPasteCount);
    countRow->addStretch(1);
    limitsForm->addRow(tr("Entries in quick paste menu:"), countRow);
    m_quickPasteTwoLine = new QCheckBox(
        tr("Two-line rows in quick paste (preview + type · app · age)"), limitsBox);
    m_quickPasteTwoLine->setToolTip(tr("Shows a second meta line per row. Off = single-line rows."));
    limitsForm->addRow(QString(), m_quickPasteTwoLine);
    auto *countHint = makeHint(QString(), limitsBox);
    limitsForm->addRow(QString(), countHint);
    connect(m_quickPasteCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [countHint,this](int v){
        countHint->setText(tr("Quick paste will show <b>%1</b> entries — press 1…%1 to paste, Enter for selected. The popup shows the most recent entries; Filter still applies (pinned, group).").arg(v));
    });
    layout->addWidget(limitsBox);

    auto *appsBox = new QGroupBox(tr("Ignored apps"), page);
    auto *appsLayout = new QVBoxLayout(appsBox);
    auto *ignoredLabel = new QLabel(tr("Ignore clipboard from these apps (one per line, <b>*</b> wildcard supported):"), appsBox);
    ignoredLabel->setWordWrap(true);
    ignoredLabel->setTextFormat(Qt::RichText);
    appsLayout->addWidget(ignoredLabel);
    m_ignoredApps = new QPlainTextEdit(appsBox);
    m_ignoredApps->setPlaceholderText(tr("e.g.\norg.keepassxc.KeePassXC\n1Password\nfirefox*\ncom.github.*\norg.mozilla.firefox"));
    m_ignoredApps->setMaximumHeight(96);
    appsLayout->addWidget(m_ignoredApps);
    appsLayout->addWidget(makeHint(tr("Source app comes from the window tracker — <b>X11:</b> process name via <code>_NET_WM_PID</code>, <b>Wayland:</b> <code>app_id</code> (e.g. <code>org.kde.kate</code>). Leave empty to capture everything. Password managers should be ignored. Tester below checks <code>isSourceIgnored</code> live."), appsBox));

    // Suggestions row: shows apps seen in history
    auto *suggestRow = new QHBoxLayout();
    m_appSuggestions = new QListWidget(appsBox);
    m_appSuggestions->setMaximumHeight(64);
    m_appSuggestions->setSelectionMode(QAbstractItemView::SingleSelection);
    m_appSuggestions->setToolTip(tr("Apps seen in your history — double-click to add to ignore list"));
    suggestRow->addWidget(m_appSuggestions, 1);
    m_addIgnoreBtn = new QPushButton(tr("Add → Ignore"), appsBox);
    m_addIgnoreBtn->setToolTip(tr("Add selected app to ignore list"));
    suggestRow->addWidget(m_addIgnoreBtn);
    appsLayout->addLayout(suggestRow);
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
    connect(m_addIgnoreBtn, &QPushButton::clicked, this, [this]{
        auto *cur = m_appSuggestions->currentItem();
        if (cur) emit m_appSuggestions->itemDoubleClicked(cur);
    });
    // live tester
    auto *testerRow = new QHBoxLayout();
    auto *testerEdit = new QLineEdit(appsBox);
    testerEdit->setPlaceholderText(tr("Test app id, e.g. org.keepassxc.KeePassXC"));
    auto *testerResult = new QLabel(appsBox);
    testerRow->addWidget(testerEdit, 1);
    testerRow->addWidget(testerResult);
    appsLayout->addLayout(testerRow);
    connect(testerEdit, &QLineEdit::textChanged, this, [this, testerResult, testerEdit](const QString &t){
        const bool ignored = m_ctx.settings()->isSourceIgnored(t);
        // also test against current edit buffer without saving
        QStringList cur = m_ignoredApps->toPlainText().split(QRegularExpression(QStringLiteral("[\n,]+")), Qt::SkipEmptyParts);
        bool live = false;
        for (const QString &pat : cur) {
            if (pat.compare(t, Qt::CaseInsensitive)==0) live=true;
            if (pat.contains(QLatin1Char('*'))) {
                QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pat), QRegularExpression::CaseInsensitiveOption);
                if (re.match(t).hasMatch()) live=true;
            }
        }
        testerResult->setText(t.isEmpty()?QString(): (live||ignored ? tr("<b>ignored</b> — will skip") : tr("captured")));
    });

    layout->addWidget(appsBox);

    auto *captureResetRow = new QHBoxLayout();
    auto *captureResetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                            tr("Reset this page to defaults"), page);
    captureResetBtn->setToolTip(tr("Restores capture types, limits and ignored apps to their defaults. Existing history is kept."));
    captureResetBtn->setAccessibleName(tr("Reset Capture page to defaults"));
    captureResetBtn->setAccessibleDescription(
        tr("Restores capture types, limits and ignored apps to their defaults."));
    connect(captureResetBtn, &QPushButton::clicked, this, [this] {
        resetPageToDefaults(SettingsManager::SettingsPage::Capture);
    });
    captureResetRow->addWidget(captureResetBtn);
    captureResetRow->addStretch(1);
    layout->addLayout(captureResetRow);

    layout->addStretch(1);
    return makeScrollable(page);
}

// --- Privacy (sensitive data, encryption) ------------------------------------
QWidget *SettingsDialog::buildPrivacyPage()
{
    auto *historyPage = new QWidget(this);
    auto *historyLayout = new QVBoxLayout(historyPage);

    auto *privacyBox = new QGroupBox(tr("Sensitive data"), historyPage);
    auto *privacyLayout = new QVBoxLayout(privacyBox);
    m_sensitiveOff = new QRadioButton(tr("Keep everything without checks"), privacyBox);
    m_sensitiveMark = new QRadioButton(tr("Store but mark (credit cards, passwords, tokens)…"), privacyBox);
    m_sensitiveRedact = new QRadioButton(tr("Redact before storing (secrets become ••••)…"), privacyBox);
    m_sensitiveExclude = new QRadioButton(tr("Never store sensitive content"), privacyBox);
    privacyLayout->addWidget(m_sensitiveOff);
    privacyLayout->addWidget(m_sensitiveMark);
    privacyLayout->addWidget(m_sensitiveRedact);
    privacyLayout->addWidget(m_sensitiveExclude);
    privacyLayout->addWidget(makeHint(tr("Detection: Luhn-validated credit cards, high-entropy secrets, API tokens (e.g. <code>AKIA…</code>, <code>ghp_…</code>, <code>sk-…</code>). <i>Exclude</i> is recommended for shared machines. <i>Redact</i> keeps the context but replaces the secret with <code>••••</code> — the original never touches disk. Custom regex below extends detection."), privacyBox));

    // Per-kind redaction toggles (only meaningful in Redact mode).
    auto *kindLabel = new QLabel(tr("Redact these kinds (Redact mode):"), privacyBox);
    privacyLayout->addWidget(kindLabel);
    auto *kindRow = new QHBoxLayout();
    kindRow->setSpacing(12);
    const QStringList allKinds = SensitiveDataDetector::allKinds();
    for (const QString &kind : allKinds) {
        auto *box = new QCheckBox(kind, privacyBox);
        box->setToolTip(kind);
        kindRow->addWidget(box);
        m_redactKindBoxes.append(box);
    }
    kindRow->addStretch(1);
    privacyLayout->addLayout(kindRow);
    auto *redactTestRow = new QHBoxLayout();
    m_redactTestBtn = new QPushButton(tr("Test redaction"), privacyBox);
    m_redactTestResult = new QLabel(privacyBox);
    m_redactTestResult->setWordWrap(true);
    m_redactTestResult->setTextFormat(Qt::RichText);
    redactTestRow->addWidget(m_redactTestBtn);
    redactTestRow->addWidget(m_redactTestResult, 1);
    privacyLayout->addLayout(redactTestRow);
    connect(m_redactTestBtn, &QPushButton::clicked, this, [this] {
        const QString sample = QStringLiteral("Card: 4111 1111 1111 1111\npassword=hunter2\nAKIAIOSFODNN7EXAMPLE");
        QStringList enabled;
        for (QCheckBox *box : m_redactKindBoxes)
            if (box->isChecked())
                enabled << box->text();
        const auto result = SensitiveDataDetector::redact(sample, enabled);
        QString html = QStringLiteral("<b style='%1'>Result:</b><br/><pre>%2</pre>")
                           .arg(UiHelpers::positiveStyle(), result.text.toHtmlEscaped());
        m_redactTestResult->setText(html);
    });
    for (QRadioButton *radio : {m_sensitiveOff, m_sensitiveMark, m_sensitiveRedact, m_sensitiveExclude})
        connect(radio, &QRadioButton::toggled, this, [this](bool) { updateRedactUi(); });

    auto *customRow = new QVBoxLayout();
    customRow->addWidget(new QLabel(tr("Custom sensitive patterns (one per line, QRegularExpression, case-insensitive):"), privacyBox));
    m_customPatterns = new QPlainTextEdit(privacyBox);
    m_customPatterns->setPlaceholderText(tr("e.g.\nmy-secret-.*\nAKIA[0-9A-Z]{16}\npassword\\s*[:=]"));
    m_customPatterns->setMaximumHeight(80);
    customRow->addWidget(m_customPatterns);
    auto *testRow = new QHBoxLayout();
    m_testSensitiveBtn = new QPushButton(tr("Test detection"), privacyBox);
    m_sensitiveTestResult = new QLabel(privacyBox);
    m_sensitiveTestResult->setWordWrap(true);
    m_sensitiveTestResult->setTextFormat(Qt::RichText);
    testRow->addWidget(m_testSensitiveBtn);
    testRow->addWidget(m_sensitiveTestResult, 1);
    customRow->addLayout(testRow);
    connect(m_testSensitiveBtn, &QPushButton::clicked, this, [this]{
        const QString sample = QStringLiteral("sample 4111 1111 1111 1111 and AKIAIOSFODNN7EXAMPLE");
        bool hit = SensitiveDataDetector::isSensitive(sample);
        QStringList pats = m_customPatterns->toPlainText().split(QRegularExpression(QStringLiteral("[\n,]+")), Qt::SkipEmptyParts);
        bool customHit = false;
        for (const QString &pat : pats) {
            QRegularExpression re(pat.trimmed(), QRegularExpression::CaseInsensitiveOption);
            if (re.isValid() && re.match(sample).hasMatch()) customHit = true;
        }
        m_sensitiveTestResult->setText((hit||customHit) ? tr("<b style='%1'>Would be flagged ✓</b> (%2)").arg(UiHelpers::positiveStyle(), hit?tr("built-in"):tr("custom")) : tr("No match — pattern not triggered"));
    });
    privacyLayout->addLayout(customRow);
    historyLayout->addWidget(privacyBox);

    auto *encryptBox = new QGroupBox(tr("Encryption at rest (SQLCipher, opt-in)"), historyPage);
    auto *encryptLayout = new QVBoxLayout(encryptBox);
    m_encryptionEnabled = new QCheckBox(tr("Encrypt database at rest (key in KWallet)"), encryptBox);
    m_encryptionEnabled->setToolTip(tr("When enabled the DB file is encrypted with SQLCipher; key held in KWallet folder egoboard / entry dbKey. Requires build -DEGOBOARD_USE_SQLCIPHER=ON."));
    encryptLayout->addWidget(m_encryptionEnabled);
    m_encryptionStatus = makeStatusPanel(QString(), encryptBox);
    encryptLayout->addWidget(m_encryptionStatus);
    auto *encRow = new QHBoxLayout();
    m_encryptionSetupBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("security-medium")), tr("Generate / store key"), encryptBox);
    m_encryptionRemoveBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Remove key"), encryptBox);
    encRow->addWidget(m_encryptionSetupBtn);
    encRow->addWidget(m_encryptionRemoveBtn);
    encRow->addStretch(1);
    encryptLayout->addLayout(encRow);
    encryptLayout->addWidget(makeHint(tr("Key loss = data loss. The app never writes the key to egoboardrc. Build without SQLCipher keeps history as before."), encryptBox));
    connect(m_encryptionSetupBtn, &QPushButton::clicked, this, [this]{
        EncryptionManager enc;
        QString existing;
        if (enc.readKey(&existing) == EncryptionManager::Status::Ok && !existing.isEmpty()) {
            QMessageBox::information(this, tr("Encryption"),
                                     tr("A key is already stored in KWallet (folder egoboard / dbKey). "
                                        "Tick the checkbox and press Apply to encrypt the database with it."));
            return;
        }
        const QString key = EncryptionManager::generateKey();
        const auto st = enc.writeKey(key);
        if (st == EncryptionManager::Status::Ok)
            QMessageBox::information(this, tr("Encryption"), tr("New key stored in KWallet (folder egoboard / dbKey). Tick the checkbox and press Apply to encrypt the existing database with it."));
        else
            QMessageBox::warning(this, tr("Encryption"), tr("Could not store key: %1").arg(enc.walletStatusText()));
        refreshDiagnostics();
    });
    connect(m_encryptionRemoveBtn, &QPushButton::clicked, this, [this]{
        if (m_ctx.storage()->isEncrypted() || m_ctx.storage()->requiresEncryptionKey()) {
            QMessageBox::warning(this, tr("Encryption"),
                                 tr("Decrypt the database first (untick encryption and press Apply). "
                                    "Removing the key now would make the history unreadable."));
            return;
        }
        if (QMessageBox::question(this, tr("Encryption"), tr("Remove the KWallet key?")) != QMessageBox::Yes) return;
        EncryptionManager enc;
        if (enc.removeKey() != EncryptionManager::Status::Ok)
            QMessageBox::warning(this, tr("Encryption"), tr("Could not remove key: %1").arg(enc.walletStatusText()));
        refreshDiagnostics();
    });
    historyLayout->addWidget(encryptBox);

    auto *privacyResetRow = new QHBoxLayout();
    auto *privacyResetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                            tr("Reset this page to defaults"), historyPage);
    privacyResetBtn->setToolTip(tr("Restores sensitive-data handling to its defaults. The encryption checkbox is left untouched."));
    privacyResetBtn->setAccessibleName(tr("Reset Privacy page to defaults"));
    privacyResetBtn->setAccessibleDescription(
        tr("Restores sensitive-data handling to its defaults without changing encryption."));
    connect(privacyResetBtn, &QPushButton::clicked, this, [this] {
        resetPageToDefaults(SettingsManager::SettingsPage::Privacy);
    });
    privacyResetRow->addWidget(privacyResetBtn);
    privacyResetRow->addStretch(1);
    historyLayout->addLayout(privacyResetRow);

    historyLayout->addStretch(1);
    return makeScrollable(historyPage);
}

// --- History (retention limits, auto-expire rules) ----------------------------
QWidget *SettingsDialog::buildHistoryPage()
{
    auto *historyPage = new QWidget(this);
    auto *historyLayout = new QVBoxLayout(historyPage);

    auto *limitsBox = new QGroupBox(tr("Retention limits"), historyPage);
    auto *limitsForm = new QFormLayout(limitsBox);
    limitsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_maxEntries = new QSpinBox(limitsBox);
    m_maxEntries->setRange(0, 1000000);
    m_maxEntries->setSingleStep(100);
    m_maxEntries->setSpecialValueText(tr("Unlimited"));
    m_maxEntries->setSuffix(tr(" entries"));
    limitsForm->addRow(tr("Keep at most:"), m_maxEntries);
    m_diskCapMb = new QSpinBox(limitsBox);
    m_diskCapMb->setRange(0, 1024 * 64);
    m_diskCapMb->setSpecialValueText(tr("Unlimited"));
    m_diskCapMb->setSuffix(tr(" MB"));
    limitsForm->addRow(tr("Total history size cap:"), m_diskCapMb);
    auto *capHint = makeHint(QString(), limitsBox);
    limitsForm->addRow(QString(), capHint);
    connect(m_diskCapMb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, capHint](int v){
        if (v==0) capHint->setText(tr("No cap — history grows forever (until disk full)."));
        else {
            qint64 cap = qint64(v)*1024*1024;
            qint64 size = m_ctx.storage()->databaseFileSize();
            if (size > cap) capHint->setText(tr("<b>Would prune ~%1</b> — DB %2 > cap %3. Oldest non-pinned entries will be removed on next check.").arg(humanSize(size-cap), humanSize(size), humanSize(cap)));
            else capHint->setText(tr("Cap %1 — DB %2 within cap, no pruning now.").arg(humanSize(cap), humanSize(size)));
        }
    });
    limitsForm->addRow(QString(), makeHint(tr("When a limit is set, the oldest non-pinned entries are removed to stay below it — pinned entries are never pruned. Checks run every 25 captures and daily for VACUUM. History is unlimited by default — the virtualized list handles 50k+ entries smoothly."), limitsBox));
    historyLayout->addWidget(limitsBox);

    auto *expireBox = new QGroupBox(tr("Auto-expire rules"), historyPage);
    auto *expireLayout = new QVBoxLayout(expireBox);
    expireLayout->addWidget(makeHint(tr("Delete old entries that match all criteria — e.g. \"unpinned Terminal copies after 24h\". Runs on startup and every 15 minutes while egoboard is running."), expireBox));
    m_expireList = new QListWidget(expireBox);
    m_expireList->setMaximumHeight(104);
    m_expireList->setSelectionMode(QAbstractItemView::SingleSelection);
    expireLayout->addWidget(m_expireList);
    auto *expireForm = new QGridLayout();
    m_expireType = new QComboBox(expireBox);
    m_expireType->addItem(tr("Any type"), -1);
    m_expireType->addItem(tr("Text"), int(ContentType::Text));
    m_expireType->addItem(tr("Rich text"), int(ContentType::RichText));
    m_expireType->addItem(tr("Image"), int(ContentType::Image));
    m_expireType->addItem(tr("Files"), int(ContentType::Files));
    m_expireApp = new QLineEdit(expireBox);
    m_expireApp->setPlaceholderText(tr("App wildcard e.g. org.kde.konsole* (empty = any)"));
    m_expireAgeH = new QSpinBox(expireBox);
    m_expireAgeH->setRange(1, 24 * 365);
    m_expireAgeH->setValue(24);
    m_expireAgeH->setSuffix(tr(" h"));
    m_expireKeepPinned = new QCheckBox(tr("Keep pinned"), expireBox);
    m_expireKeepPinned->setChecked(true);
    auto *addExpireBtn = new QPushButton(tr("Add rule"), expireBox);
    auto *removeExpireBtn = new QPushButton(tr("Remove"), expireBox);
    expireForm->addWidget(m_expireType, 0, 0);
    expireForm->addWidget(m_expireApp, 0, 1);
    expireForm->addWidget(m_expireAgeH, 0, 2);
    expireForm->addWidget(m_expireKeepPinned, 0, 3);
    expireForm->addWidget(addExpireBtn, 0, 4);
    expireForm->addWidget(removeExpireBtn, 1, 4);
    expireForm->setColumnStretch(1, 1);
    expireLayout->addLayout(expireForm);
    connect(addExpireBtn, &QPushButton::clicked, this, [this] {
        ExpireRule rule;
        rule.contentType = m_expireType->currentData().toInt();
        rule.sourceAppWildcard = m_expireApp->text().trimmed();
        rule.ageSeconds = qint64(m_expireAgeH->value()) * 3600;
        rule.keepPinned = m_expireKeepPinned->isChecked();
        m_expireRules.append(rule);
        refreshExpireList();
    });
    connect(removeExpireBtn, &QPushButton::clicked, this, [this] {
        const int row = m_expireList->currentRow();
        if (row >= 0 && row < m_expireRules.size()) {
            m_expireRules.removeAt(row);
            refreshExpireList();
        }
    });
    historyLayout->addWidget(expireBox);

    auto *historyResetRow = new QHBoxLayout();
    auto *historyResetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                            tr("Reset this page to defaults"), historyPage);
    historyResetBtn->setToolTip(tr("Restores retention limits and clears auto-expire rules. History entries are kept."));
    historyResetBtn->setAccessibleName(tr("Reset History page to defaults"));
    historyResetBtn->setAccessibleDescription(
        tr("Restores retention limits and clears auto-expire rules."));
    connect(historyResetBtn, &QPushButton::clicked, this, [this] {
        resetPageToDefaults(SettingsManager::SettingsPage::History);
    });
    historyResetRow->addWidget(historyResetBtn);
    historyResetRow->addStretch(1);
    historyLayout->addLayout(historyResetRow);

    historyLayout->addStretch(1);
    return makeScrollable(historyPage);
}

QWidget *SettingsDialog::buildUsagePage()
{
    // P2-A dashboard, embedded: the shared read-only panel (counts by day,
    // app, type and size plus streak — entry text never leaves the database).
    // No knobs, so no load()/save()/reset wiring; the constructor visits row 0
    // and the currentRowChanged hook above refreshes on every later visit.
    // The panel scrolls internally, so unlike the knob pages it is returned
    // directly instead of through makeScrollable().
    m_usagePanel = new DashboardPanel(m_ctx.storage(), this);
    return m_usagePanel;
}

QWidget *SettingsDialog::buildSearchPreviewPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *ftsBox = new QGroupBox(tr("Full-text search (FTS5)"), page);
    auto *ftsLayout = new QVBoxLayout(ftsBox);
    m_ftsStatus = new QLabel(tr("Checking index…"), ftsBox);
    m_ftsStatus->setWordWrap(true);
    m_ftsStatus->setTextFormat(Qt::RichText);
    ftsLayout->addWidget(m_ftsStatus);
    ftsLayout->addWidget(makeHint(tr("Index covers <code>preview</code>, <code>text_data</code> and <code>ocr_text</code> with <code>unicode61</code> tokenizer, external-content sync and prefix search (<code>\"token\"*</code>). Queries like <code>hello world</code> become <code>\"hello\"* AND \"world\"*</code> — every word must match, diacritics folded."), ftsBox));
    auto *ftsRow = new QHBoxLayout();
    m_ftsRebuildBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Rebuild index"), ftsBox);
    m_ftsRebuildBtn->setToolTip(tr("Runs INSERT INTO entries_fts(entries_fts) VALUES('rebuild') — safe, handles external-content drift."));
    m_ftsOptimizeBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("system-run")), tr("Optimize"), ftsBox);
    m_ftsOptimizeBtn->setToolTip(tr("Runs INSERT INTO entries_fts(entries_fts) VALUES('optimize') — merges b-tree, faster searches."));
    ftsRow->addWidget(m_ftsRebuildBtn);
    ftsRow->addWidget(m_ftsOptimizeBtn);
    ftsRow->addStretch(1);
    ftsLayout->addLayout(ftsRow);
    connect(m_ftsRebuildBtn, &QPushButton::clicked, this, [this]{
        m_ftsStatus->setText(tr("Rebuilding…"));
        m_ftsRebuildBtn->setEnabled(false);
        QSqlDatabase db = m_ctx.storage()->database();
        QSqlQuery q(db);
        const bool ok = q.exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES('rebuild')"));
        if (ok) m_ftsStatus->setText(tr("<b style='%1'>Rebuilt ✓</b> — index now in sync.").arg(UiHelpers::positiveStyle()));
        else m_ftsStatus->setText(tr("<b>Rebuild failed:</b> %1").arg(q.lastError().text()));
        m_ftsRebuildBtn->setEnabled(true);
        QTimer::singleShot(3000, this, &SettingsDialog::refreshDiagnostics);
    });
    connect(m_ftsOptimizeBtn, &QPushButton::clicked, this, [this]{
        QSqlDatabase db = m_ctx.storage()->database();
        QSqlQuery q(db);
        q.exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES('optimize')"));
        m_ftsStatus->setText(tr("Optimized — b-tree merged."));
        QTimer::singleShot(3000, this, &SettingsDialog::refreshDiagnostics);
    });
    // query tester
    auto *testerRow = new QHBoxLayout();
    auto *testerEdit = new QLineEdit(ftsBox);
    testerEdit->setPlaceholderText(tr("Test query, e.g. app:firefox invoice -draft"));
    auto *testerResult = new QLabel(ftsBox);
    testerResult->setTextFormat(Qt::RichText);
    testerRow->addWidget(testerEdit, 1);
    testerRow->addWidget(testerResult);
    ftsLayout->addLayout(testerRow);
    connect(testerEdit, &QLineEdit::textChanged, this, [this, testerResult](const QString &t){
        if (t.trimmed().isEmpty()) { testerResult->clear(); return; }
        // Explain the typed query: field filters, rejected values, the FTS5
        // expression it becomes, the hit count and how long the match took.
        const SearchEngine::ParsedQuery parsed = SearchEngine::parseQuery(t.trimmed());
        QStringList lines;
        if (!parsed.applied.isEmpty())
            lines << tr("filters: %1").arg(parsed.applied.join(QStringLiteral(" · ")).toHtmlEscaped());
        if (!parsed.problems.isEmpty())
            lines << QStringLiteral("<b>%1</b>").arg(parsed.problems.join(QStringLiteral(" · ")).toHtmlEscaped());
        if (!parsed.filter.excludeText.isEmpty())
            lines << tr("excluding: <code>%1</code>").arg(parsed.filter.excludeText.toHtmlEscaped());

        QSqlDatabase db = m_ctx.storage()->database();
        const QString ftsQuery = SearchEngine::buildFtsQuery(parsed.filter.searchText);
        if (!ftsQuery.isEmpty()) {
            if (!SearchEngine::isFtsAvailable(db)) {
                lines << tr("<b>FTS5 unavailable</b> — substring (LIKE) search is used instead");
            } else {
                // Same query the list uses, plus how many entries it matches.
                QElapsedTimer timer;
                timer.start();
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT COUNT(*) FROM entries_fts WHERE entries_fts MATCH :q"));
                q.bindValue(QStringLiteral(":q"), ftsQuery);
                if (!q.exec() || !q.next()) {
                    lines << tr("FTS: <code>%1</code> — <b>invalid query</b>")
                                 .arg(ftsQuery.toHtmlEscaped());
                } else {
                    const qint64 hits =
                        qMin<qint64>(q.value(0).toLongLong(), std::numeric_limits<int>::max());
                    lines << tr("FTS: <code>%1</code> — %n hit(s) · %2 ms", nullptr, int(hits))
                                 .arg(ftsQuery.toHtmlEscaped())
                                 .arg(timer.elapsed());
                }
            }
        }
        testerResult->setText(lines.join(QStringLiteral("<br/>")));
    });
    layout->addWidget(ftsBox);

    auto *previewBox = new QGroupBox(tr("Preview enrichments"), page);
    auto *previewLayout = new QVBoxLayout(previewBox);
    m_previewCode = new QCheckBox(tr("Syntax highlight code (JSON/XML/generic)"), previewBox);
    m_previewLinks = new QCheckBox(tr("Linkify URLs (🔗 clickable)"), previewBox);
    m_previewColors = new QCheckBox(tr("Show color swatches for #RRGGBB (🎨)"), previewBox);
    m_timelineEnabled = new QCheckBox(tr("Show timeline strip (14-day histogram above the list)"), previewBox);
    m_timelineEnabled->setToolTip(tr("Thin histogram above the list — click a bar to filter that day. Hiding it leaves more room for the list."));
    previewLayout->addWidget(m_previewCode);
    previewLayout->addWidget(m_previewLinks);
    previewLayout->addWidget(m_previewColors);
    previewLayout->addWidget(m_timelineEnabled);
    // The sample swatch uses the scheme's own accent instead of a fixed red,
    // so it demonstrates the palette on whatever theme is loaded.
    m_previewSample = makeStatusPanel(QString(), previewBox);
    {
        const QColor accent = qApp->palette().color(QPalette::Highlight);
        const QColor onAccent = qApp->palette().color(QPalette::HighlightedText);
        m_previewSample->setText(
            tr("Sample: <code>{\"a\": 1}</code> → highlighted keys, "
               "<a href=\"https://example.com\">https://example.com</a> → 🔗, "
               "<span style=\"background:%1; color:%2; padding:0 6px; border-radius:3px;\">%1</span> "
               "→ 🎨. Toggle above to disable.")
                .arg(accent.name(), onAccent.name()));
    }
    previewLayout->addWidget(m_previewSample);
    previewLayout->addWidget(makeHint(tr("All detectors are local regex. Disabling restores raw text preview and speeds up rendering for huge entries."), previewBox));
    layout->addWidget(previewBox);

    auto *ocrBox = new QGroupBox(tr("OCR — image text recognition (local tesseract)"), page);
    auto *ocrLayout = new QVBoxLayout(ocrBox);
    m_ocrEnabled = new QCheckBox(tr("Enable OCR for images (searchable)"), ocrBox);
    m_ocrEnabled->setToolTip(tr("When enabled, copied images are OCR'd in the background and become searchable via FTS. Requires tesseract — no network ever."));
    ocrLayout->addWidget(m_ocrEnabled);
    auto *ocrForm = new QFormLayout();
    ocrForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_ocrLang = new QComboBox(ocrBox);
    m_ocrLang->setEditable(true);
    m_ocrLang->addItems({QStringLiteral("eng"), QStringLiteral("eng+deu"), QStringLiteral("deu"), QStringLiteral("fra"), QStringLiteral("spa"), QStringLiteral("jpn"), QStringLiteral("chi_sim")});
    m_ocrLang->setToolTip(tr("tesseract -l value, e.g. eng or eng+deu. Requires matching tessdata."));
    ocrForm->addRow(tr("OCR language:"), m_ocrLang);
    m_ocrMaxChars = new QSpinBox(ocrBox);
    m_ocrMaxChars->setRange(512, 65536);
    m_ocrMaxChars->setSingleStep(512);
    ocrForm->addRow(tr("Max OCR chars per image:"), m_ocrMaxChars);
    ocrLayout->addLayout(ocrForm);
    m_ocrStatus = makeStatusPanel(tr("Checking tesseract…"), ocrBox);
    ocrLayout->addWidget(m_ocrStatus);
    auto *ocrHint = makeHint(tr("Images with text (screenshots, slides) become searchable. OCR runs at most once per image on a thread pool, capped above. In preview you’ll see <i>🔍 OCR:</i> under images. Recognized text is stored in <code>entries.ocr_text</code>, FTS-indexed."), ocrBox);
    ocrLayout->addWidget(ocrHint);
    m_testOcrBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("image-x-generic")), tr("Test OCR with sample image"), ocrBox);
    ocrLayout->addWidget(m_testOcrBtn);
    connect(m_testOcrBtn, &QPushButton::clicked, this, [this]{
        m_ocrStatus->setText(tr("Testing…"));
        if (!OcrWorker::isAvailable()) {
            m_ocrStatus->setText(tr("<b style='%1'>tesseract not found</b> — install <code>tesseract</code> and <code>tesseract-data-eng</code>").arg(UiHelpers::warningStyle()));
            return;
        }
        m_ocrStatus->setText(tr("Checking tesseract…"));
        QPointer<SettingsDialog> guard(this);
        QThreadPool::globalInstance()->start([guard]{
            const QString ver = tesseractVersion();
            QMetaObject::invokeMethod(qApp, [guard, ver]{
                if (!guard) return;
                if (!guard->m_ocrStatus) return;
                guard->m_ocrStatus->setText(tr("<b style='%1'>tesseract OK</b> — %2").arg(UiHelpers::positiveStyle(), ver.isEmpty() ? QStringLiteral("found in PATH") : ver));
            }, Qt::QueuedConnection);
        });
    });
    layout->addWidget(ocrBox);

    auto *previewResetRow = new QHBoxLayout();
    auto *previewResetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                            tr("Reset this page to defaults"), page);
    previewResetBtn->setToolTip(tr("Restores preview enrichments, timeline and OCR options to their defaults."));
    previewResetBtn->setAccessibleName(tr("Reset Search and Preview page to defaults"));
    previewResetBtn->setAccessibleDescription(
        tr("Restores preview enrichments, timeline and OCR options to their defaults."));
    connect(previewResetBtn, &QPushButton::clicked, this, [this] {
        resetPageToDefaults(SettingsManager::SettingsPage::SearchPreview);
    });
    previewResetRow->addWidget(previewResetBtn);
    previewResetRow->addStretch(1);
    layout->addLayout(previewResetRow);

    layout->addStretch(1);
    return makeScrollable(page);
}

QWidget *SettingsDialog::buildStoragePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *dbInfoBox = new QGroupBox(tr("Database"), page);
    auto *dbLayout = new QVBoxLayout(dbInfoBox);
    const QString path = m_ctx.storage()->databasePath();
    auto *pathLabel = new QLabel(dbInfoBox);
    pathLabel->setTextFormat(Qt::RichText);
    // The link must be a real anchor: linkActivated only fires for <a href>.
    pathLabel->setText(tr("File: <a href=\"%1\"><code>%2</code></a> — click to open folder")
                           .arg(QUrl::fromLocalFile(QFileInfo(path).absolutePath()).toString(),
                                path.toHtmlEscaped()));
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    pathLabel->setCursor(Qt::PointingHandCursor);
    dbLayout->addWidget(pathLabel);
    connect(pathLabel, &QLabel::linkActivated, this, [path]{
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    auto *sizeLabel = new QLabel(dbInfoBox);
    sizeLabel->setTextFormat(Qt::RichText);
    sizeLabel->setWordWrap(true);
    dbLayout->addWidget(sizeLabel);
    // size updated in refreshDiagnostics
    sizeLabel->setObjectName(QStringLiteral("dbSizeLabel"));
    dbLayout->addWidget(makeHint(tr("FTS and OCR are local — no cloud. The database lives under <code>~/.local/share/egoboard/</code> (WAL mode, foreign keys on)."), dbInfoBox));
    // pragma badges
    auto *pragmaLabel = makeHint(QString(), dbInfoBox);
    pragmaLabel->setObjectName(QStringLiteral("pragmaLabel"));
    dbLayout->addWidget(pragmaLabel);
    layout->addWidget(dbInfoBox);

    auto *backupBox = new QGroupBox(tr("Automatic backups"), page);
    auto *backupLayout = new QVBoxLayout(backupBox);
    m_backupEnabled = new QCheckBox(tr("Write a daily JSON backup"), backupBox);
    m_backupEnabled->setToolTip(tr("Exports the whole history (including OCR text, tags, groups, snippets and saved searches) into the backup folder once a day."));
    backupLayout->addWidget(m_backupEnabled);

    auto *folderRow = new QHBoxLayout();
    folderRow->addWidget(new QLabel(tr("Folder:"), backupBox));
    m_backupFolder = new QLineEdit(backupBox);
    m_backupFolder->setPlaceholderText(SettingsManager::defaultBackupFolder());
    m_backupFolder->setToolTip(tr("Where backup files are written. Empty uses the default folder shown here."));
    folderRow->addWidget(m_backupFolder, 1);
    auto *browseBackupBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-open")),
                                            tr("Choose…"), backupBox);
    connect(browseBackupBtn, &QPushButton::clicked, this, [this] {
        const QString start = m_backupFolder->text().trimmed().isEmpty()
            ? SettingsManager::defaultBackupFolder()
            : m_backupFolder->text().trimmed();
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Backup folder"), start);
        if (!dir.isEmpty())
            m_backupFolder->setText(dir);
    });
    folderRow->addWidget(browseBackupBtn);
    backupLayout->addLayout(folderRow);

    auto *keepRow = new QHBoxLayout();
    keepRow->addWidget(new QLabel(tr("Keep the newest:"), backupBox));
    m_backupKeep = new QSpinBox(backupBox);
    m_backupKeep->setRange(1, 100);
    m_backupKeep->setSuffix(tr(" files"));
    m_backupKeep->setToolTip(tr("Older backups beyond this count are deleted after each run."));
    keepRow->addWidget(m_backupKeep);
    keepRow->addStretch(1);
    // Works even when the daily schedule is off.
    m_backupNowBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                     tr("Back up now"), backupBox);
    keepRow->addWidget(m_backupNowBtn);
    auto *restoreBackupBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-revert")),
                                             tr("Restore…"), backupBox);
    restoreBackupBtn->setToolTip(tr("Pick one of the backups in the folder and apply it."));
    connect(restoreBackupBtn, &QPushButton::clicked, this, [this] {
        BackupService *service = m_ctx.backupService();
        if (!service)
            return;
        const QString folder = service->folder();
        const QStringList backups = ExportImportManager::listBackups(folder);
        if (backups.isEmpty()) {
            QMessageBox::information(this, tr("Restore"),
                                     tr("No backups found in %1.").arg(folder));
            return;
        }
        ExportImportDialogs::RestoreBackupDialog dialog(backups, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QString path = dialog.selectedPath();
        if (path.isEmpty())
            return;
        if (!service->restoreNow(path, dialog.mode())) {
            QMessageBox::information(this, tr("Restore"), tr("A backup or restore is already running."));
            return;
        }
        m_backupStatus->setText(tr("Restoring %1…").arg(QFileInfo(path).fileName()));
        showIoProgress(tr("Restoring %1…").arg(QFileInfo(path).fileName()));
    });
    keepRow->addWidget(restoreBackupBtn);
    backupLayout->addLayout(keepRow);

    // U14 settings portability, next to the history backup: the whole setup
    // as one JSON file. Machine-local state (last backup time) is excluded;
    // secrets stay in KWallet and never enter the file.
    auto *settingsRow = new QHBoxLayout();
    settingsRow->addWidget(new QLabel(tr("Settings:"), backupBox));
    auto *exportSettingsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                              tr("Export settings…"), backupBox);
    connect(exportSettingsBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export settings"), SettingsManager::defaultBackupFolder()
                + QStringLiteral("/egoboard-settings.json"),
            tr("Egoboard settings (*.json)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Export settings"),
                                 tr("Cannot write %1: %2").arg(path, file.errorString()));
            return;
        }
        file.write(QJsonDocument(m_ctx.settings()->exportToJson()).toJson(QJsonDocument::Indented));
        QMessageBox::information(this, tr("Export settings"),
                                 tr("Settings exported to %1.").arg(path));
    });
    settingsRow->addWidget(exportSettingsBtn);
    auto *importSettingsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")),
                                              tr("Import settings…"), backupBox);
    connect(importSettingsBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import settings"), SettingsManager::defaultBackupFolder(),
            tr("Egoboard settings (*.json);;All files (*)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Import settings"),
                                 tr("Cannot read %1: %2").arg(path, file.errorString()));
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            QMessageBox::warning(this, tr("Import settings"),
                                 tr("%1 is not a valid Egoboard settings file.").arg(path));
            return;
        }
        QString error;
        if (!m_ctx.settings()->importFromJson(document.object(), &error)) {
            QMessageBox::warning(this, tr("Import settings"), error);
            return;
        }
        load(); // re-read every page from the imported values
        refreshDiagnostics();
        QMessageBox::information(this, tr("Import settings"), tr("Settings imported."));
    });
    settingsRow->addWidget(importSettingsBtn);
    settingsRow->addStretch(1);
    backupLayout->addLayout(settingsRow);

    // U14 profiles ("Work"/"Personal"): named setting sets stored as their
    // own KConfig groups. Save snapshots the current setup, Apply switches
    // the whole setting set (same validating round-trip as Import settings),
    // Delete removes it. The palette switches with `>profile <name>`.
    auto *profileRow = new QHBoxLayout();
    profileRow->addWidget(new QLabel(tr("Profiles:"), backupBox));
    m_profileCombo = new QComboBox(backupBox);
    m_profileCombo->setEditable(true);
    m_profileCombo->setInsertPolicy(QComboBox::NoInsert);
    m_profileCombo->setPlaceholderText(tr("Work"));
    m_profileCombo->setToolTip(tr("Named setting sets — type a name and Save, or pick one and Apply."));
    m_profileCombo->setAccessibleName(tr("Settings profiles"));
    m_profileCombo->setAccessibleDescription(
        tr("Named setting sets. Save snapshots the current setup; Apply switches to it."));
    profileRow->addWidget(m_profileCombo, 1);
    auto *saveProfileBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                           tr("Save"), backupBox);
    saveProfileBtn->setToolTip(tr("Save the current settings as this profile."));
    connect(saveProfileBtn, &QPushButton::clicked, this, [this] {
        if (!m_profileCombo)
            return;
        QString error;
        if (!m_ctx.settings()->saveProfile(m_profileCombo->currentText(), &error)) {
            QMessageBox::warning(this, tr("Save profile"), error);
            return;
        }
        refreshProfileList();
        QMessageBox::information(this, tr("Save profile"),
                                 tr("Profile “%1” saved.")
                                     .arg(m_ctx.settings()->activeProfile()));
    });
    profileRow->addWidget(saveProfileBtn);
    auto *applyProfileBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")),
                                            tr("Apply"), backupBox);
    applyProfileBtn->setToolTip(tr("Switch the whole setting set to this profile (palette: >profile <name>)."));
    connect(applyProfileBtn, &QPushButton::clicked, this, [this] {
        if (!m_profileCombo)
            return;
        QString error;
        if (!m_ctx.settings()->applyProfile(m_profileCombo->currentText(), &error)) {
            QMessageBox::warning(this, tr("Apply profile"), error);
            return;
        }
        load(); // re-read every page from the applied values
        previewThemes(); // the applied set may carry another theme pair
        populateTransformList();
        populateSnippetList();
        populateScriptList();
        refreshProfileList();
        refreshDiagnostics();
        QMessageBox::information(this, tr("Apply profile"),
                                 tr("Switched to profile “%1”.")
                                     .arg(m_ctx.settings()->activeProfile()));
    });
    profileRow->addWidget(applyProfileBtn);
    auto *deleteProfileBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                             tr("Delete"), backupBox);
    deleteProfileBtn->setToolTip(tr("Delete this profile (current settings are kept)."));
    connect(deleteProfileBtn, &QPushButton::clicked, this, [this] {
        if (!m_profileCombo)
            return;
        const QString name = SettingsManager::normalizeProfileName(
            m_profileCombo->currentText());
        if (name.isEmpty())
            return;
        if (QMessageBox::question(this, tr("Delete profile"),
                                  tr("Delete profile “%1”? The current settings are kept.")
                                      .arg(name))
            != QMessageBox::Yes)
            return;
        QString error;
        if (!m_ctx.settings()->deleteProfile(name, &error)) {
            QMessageBox::warning(this, tr("Delete profile"), error);
            return;
        }
        refreshProfileList();
    });
    profileRow->addWidget(deleteProfileBtn);
    backupLayout->addLayout(profileRow);
    backupLayout->addWidget(makeHint(tr("Profiles hold whole setting sets (Work, Personal) — switch here or from the palette with <code>&gt;profile &lt;name&gt;</code>. History entries and KWallet secrets are never stored in a profile."), backupBox));
    refreshProfileList();

    m_backupStatus = makeStatusPanel(QString(), backupBox);
    backupLayout->addWidget(m_backupStatus);
    backupLayout->addWidget(makeHint(tr("Backups are plain JSON files — restore one with Import JSON… below. The export runs on a worker thread, so the window stays responsive."), backupBox));

    connect(m_backupNowBtn, &QPushButton::clicked, this, [this] {
        BackupService *service = m_ctx.backupService();
        if (!service)
            return;
        m_backupNowBtn->setEnabled(false);
        m_backupStatus->setText(tr("Backup running…"));
        if (!service->runNow()) {
            m_backupNowBtn->setEnabled(true);
            m_backupStatus->setText(tr("A backup is already running."));
        } else {
            showIoProgress(tr("Backup running…"));
        }
    });
    if (BackupService *service = m_ctx.backupService()) {
        connect(service, &BackupService::finished, this,
                [this](bool ok, const QString &path, const QString &error) {
                    closeIoProgress();
                    m_backupNowBtn->setEnabled(true);
                    if (ok)
                        m_backupStatus->setText(tr("Last backup: %1").arg(path));
                    else
                        m_backupStatus->setText(tr("Backup failed: %1").arg(error));
                });
        connect(service, &BackupService::restoreFinished, this,
                [this](bool ok, const QString &path, const QString &error, int imported,
                       int merged, int skipped) {
                    closeIoProgress();
                    m_backupNowBtn->setEnabled(true);
                    if (ok) {
                        m_backupStatus->setText(
                            tr("Restored %1 — %2 added, %3 merged, %4 skipped.")
                                .arg(QFileInfo(path).fileName())
                                .arg(imported)
                                .arg(merged)
                                .arg(skipped));
                    } else {
                        m_backupStatus->setText(tr("Restore failed: %1").arg(error));
                    }
                });
        connect(service, &BackupService::backupProgress, this,
                [this](int done, int total) {
                    if (m_ioProgress) {
                        if (total > 0) {
                            m_ioProgress->setMaximum(total);
                            m_ioProgress->setValue(done);
                        }
                        m_ioProgress->setLabelText(
                            tr("Backup… %1 of %2 entries").arg(done).arg(total));
                    }
                });
        connect(service, &BackupService::restoreProgress, this,
                [this](int done, int total) {
                    if (m_ioProgress) {
                        if (total > 0) {
                            m_ioProgress->setMaximum(total);
                            m_ioProgress->setValue(done);
                        }
                        m_ioProgress->setLabelText(
                            tr("Restoring… %1 of %2 entries").arg(done).arg(total));
                    }
                });
    }
    layout->addWidget(backupBox);

    auto *maintenanceBox = new QGroupBox(tr("Maintenance"), page);
    auto *maintenanceLayout = new QVBoxLayout(maintenanceBox);
    m_maintenanceLayout = maintenanceLayout;
    auto *row = new QHBoxLayout();
    auto *vacuumButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Compact database now (VACUUM)"), maintenanceBox);
    connect(vacuumButton, &QPushButton::clicked, this, [this, vacuumButton] {
        vacuumButton->setEnabled(false);
        vacuumButton->setText(tr("Compacting…"));
        auto *dialog = new QProgressDialog(tr("Compacting database…"), QString(), 0, 0, this);
        dialog->setWindowModality(Qt::WindowModal);
        dialog->setMinimumDuration(0);
        dialog->setCancelButton(nullptr);
        dialog->show();
        m_ctx.vacuumNow();
        connect(&m_ctx, &ApplicationContext::vacuumFinished, this,
                [dialog, vacuumButton](bool, qint64) {
                    if (dialog) dialog->close();
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
    auto *clearHistoryBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Clear history (keep pinned)"), maintenanceBox);
    connect(clearHistoryBtn, &QPushButton::clicked, this, [this]{
        if (QMessageBox::question(this, tr("Clear history"), tr("Remove all non-pinned entries? Pinned stays. This cannot be undone."))==QMessageBox::Yes) {
            m_ctx.storage()->clearHistory(true);
            refreshDiagnostics();
        }
    });
    row->addWidget(clearHistoryBtn);
    row->addStretch(1);
    maintenanceLayout->addLayout(row);
    // Integrity: verify the file, and repair search without touching history.
    auto *integrityRow = new QHBoxLayout();
    auto *integrityBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("tools-wizard")),
                                         tr("Check integrity"), maintenanceBox);
    integrityBtn->setToolTip(tr("Runs PRAGMA quick_check and reports the first problem it finds."));
    connect(integrityBtn, &QPushButton::clicked, this, [this] {
        QString error;
        const bool ok = runWithProgress(this, tr("Checking integrity…"), [this, &error] {
            return m_ctx.storage()->quickCheck(&error);
        });
        if (ok) {
            hideIntegrityError();
            QMessageBox::information(this, tr("Integrity check"),
                                     tr("PRAGMA quick_check reports no problems."));
        } else {
            // U13 DB-error state: persistent, actionable, non-modal — the panel
            // stays until a later check passes or the index is rebuilt.
            showIntegrityError(error);
        }
    });
    integrityRow->addWidget(integrityBtn);
    auto *reindexBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                       tr("Rebuild search index"), maintenanceBox);
    reindexBtn->setToolTip(tr("Recreates the FTS5 index from the history. Safe: no entry data is touched."));
    connect(reindexBtn, &QPushButton::clicked, this, &SettingsDialog::rebuildSearchIndex);
    integrityRow->addWidget(reindexBtn);
    integrityRow->addStretch(1);
    maintenanceLayout->addLayout(integrityRow);
    maintenanceLayout->addWidget(makeHint(tr("Egoboard also compacts automatically once a day when the database grows past 50 MB. The VACUUM runs on a dedicated thread with its own connection so the UI stays responsive."), maintenanceBox));
    // export/import — full flows moved here from the main-window toolbar
    auto *ioRow = new QHBoxLayout();
    auto *exportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), tr("Export JSON…"), maintenanceBox);
    auto *importBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), tr("Import JSON…"), maintenanceBox);
    ioRow->addWidget(exportBtn);
    ioRow->addWidget(importBtn);
    auto *importKlipperBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-paste")),
                                             tr("Import Klipper…"), maintenanceBox);
    importKlipperBtn->setToolTip(tr("Imports the text entries of Klipper's history3.sqlite. "
                                    "Starred items become pinned; duplicates merge."));
    connect(importKlipperBtn, &QPushButton::clicked, this, [this] {
        const QString defaultPath = ExportImportManager::defaultKlipperPath();
        const QString start = QFileInfo::exists(defaultPath)
            ? defaultPath
            : QFileInfo(defaultPath).absolutePath();
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import Klipper history"), start,
            tr("Klipper history (history3.sqlite *.sqlite);;All files (*)"));
        if (path.isEmpty())
            return;
        const auto result = runIoWithProgress(
            this, tr("Importing Klipper history…"),
            [&](std::atomic<bool> *cancel, ExportImportManager::IoProgress progress) {
                return m_ctx.io()->importKlipperHistory(path, cancel, progress);
            });
        if (!result.ok) {
            QMessageBox::warning(this, tr("Klipper import"), result.error);
            return;
        }
        QMessageBox::information(this, tr("Klipper import"),
                                 tr("Imported %1, merged %2, skipped %3 entries.")
                                     .arg(result.entriesImported)
                                     .arg(result.entriesMerged)
                                     .arg(result.entriesSkipped));
        refreshDiagnostics();
    });
    ioRow->addWidget(importKlipperBtn);
    ioRow->addStretch(1);
    maintenanceLayout->addLayout(ioRow);
    connect(exportBtn, &QPushButton::clicked, this, [this]{
        ExportImportDialogs::ExportDialog dialog(m_ctx.bookmarks(), this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        ExportImportManager::ExportRequest request;
        request.path = dialog.filePath();
        request.format = dialog.format();
        switch (dialog.scope()) {
        case ExportImportDialogs::ExportDialog::Everything:
            request.scope = ExportImportManager::Scope::Everything;
            break;
        case ExportImportDialogs::ExportDialog::PinnedOnly:
            request.scope = ExportImportManager::Scope::PinnedOnly;
            break;
        case ExportImportDialogs::ExportDialog::GroupSubtree:
            request.scope = ExportImportManager::Scope::GroupSubtree;
            request.groupId = dialog.groupId();
            break;
        }
        QString error;
        const bool exported = runIoWithProgress(
            this, tr("Exporting…"),
            [&](std::atomic<bool> *cancel, ExportImportManager::IoProgress progress) {
                return m_ctx.io()->exportToFile(request, &error, cancel, progress);
            });
        if (!exported)
            QMessageBox::warning(this, tr("Export failed"), error);
        else
            QMessageBox::information(this, tr("Export finished"),
                                     tr("History exported to %1.").arg(request.path));
        refreshDiagnostics();
    });
    connect(importBtn, &QPushButton::clicked, this, [this]{
        ExportImportDialogs::ImportDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const auto result = runIoWithProgress(
            this, tr("Importing…"),
            [this, path = dialog.filePath(), mode = dialog.mode()](
                std::atomic<bool> *cancel, ExportImportManager::IoProgress progress) {
                return m_ctx.io()->importFromFile(path, mode, cancel, progress);
            });
        if (!result.ok) {
            QMessageBox::warning(this, tr("Import failed"), result.error);
            return;
        }
        QMessageBox::information(
            this, tr("Import finished"),
            tr("Imported %1, merged %2, skipped %3 entries; %4 group(s) imported.")
                .arg(result.entriesImported)
                .arg(result.entriesMerged)
                .arg(result.entriesSkipped)
                .arg(result.groupsImported));
        refreshDiagnostics();
    });
    layout->addWidget(maintenanceBox);

    auto *storageResetRow = new QHBoxLayout();
    auto *storageResetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                            tr("Reset this page to defaults"), page);
    storageResetBtn->setToolTip(tr("Restores automatic-backup options to their defaults. History entries and backup files are kept."));
    storageResetBtn->setAccessibleName(tr("Reset Storage page to defaults"));
    storageResetBtn->setAccessibleDescription(
        tr("Restores automatic-backup options to their defaults."));
    connect(storageResetBtn, &QPushButton::clicked, this, [this] {
        resetPageToDefaults(SettingsManager::SettingsPage::Storage);
    });
    storageResetRow->addWidget(storageResetBtn);
    storageResetRow->addStretch(1);
    layout->addLayout(storageResetRow);

    layout->addStretch(1);
    return makeScrollable(page);
}

void SettingsDialog::rebuildSearchIndex()
{
    const bool ok = runWithProgress(this, tr("Rebuilding search index…"), [this] {
        return m_ctx.storage()->rebuildSearchIndex();
    });
    if (ok) {
        hideIntegrityError();
        QMessageBox::information(this, tr("Search index"), tr("The full-text index was rebuilt."));
    } else {
        QMessageBox::warning(this, tr("Search index"),
                             tr("The full-text index could not be rebuilt."));
    }
    refreshDiagnostics();
}

void SettingsDialog::showIntegrityError(const QString &error)
{
    hideIntegrityError();
    if (!m_maintenanceLayout)
        return;
    // U13 DB-error state: icon + title + the reported problem + one action.
    // Rebuilt per failure (same pattern as the main-window empty state) so the
    // message always matches the last check. Plain text: the message comes
    // from SQLite, never from history content.
    m_integrityError = UiHelpers::makeEmptyState(
        QStringLiteral("dialog-warning"), tr("Database problem"),
        tr("quick_check reported: %1. Rebuilding fixes a damaged index; for a damaged file, "
           "import the newest backup into a fresh database.")
            .arg(error.isEmpty() ? tr("unknown error") : error),
        this, tr("Rebuild search index"), [this] { rebuildSearchIndex(); });
    m_maintenanceLayout->addWidget(m_integrityError);
}

void SettingsDialog::hideIntegrityError()
{
    if (m_integrityError) {
        m_integrityError->deleteLater();
        m_integrityError = nullptr;
    }
}

void SettingsDialog::showIoProgress(const QString &label)
{
    closeIoProgress();
    auto *dialog = new QProgressDialog(label, tr("Cancel"), 0, 0, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    connect(dialog, &QProgressDialog::canceled, this, [this] {
        if (BackupService *service = m_ctx.backupService())
            service->cancel();
    });
    dialog->show();
    m_ioProgress = dialog;
}

void SettingsDialog::closeIoProgress()
{
    if (m_ioProgress) {
        m_ioProgress->close();
        m_ioProgress->deleteLater();
        m_ioProgress = nullptr;
    }
}

void SettingsDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    applyResponsiveLayout();
}

void SettingsDialog::applyResponsiveLayout()
{
    if (!m_sidebar || !m_content)
        return;
    // Single source with the tests: under the token the icon sidebar becomes
    // a horizontal top strip; mode flips only (no per-resize churn).
    const bool narrow = DesignTokens::settingsNarrowLayoutForWidth(width());
    if (narrow == m_narrowLayout)
        return;
    m_narrowLayout = narrow;
    UiHelpers::applySidebarMode(m_sidebar, m_content, narrow,
                                 SettingsStructure::kSidebarWideWidth);
}

void SettingsDialog::clearSettingsSearchHighlight()
{
    for (auto it = m_searchFonts.constBegin(); it != m_searchFonts.constEnd(); ++it) {
        if (QWidget *widget = it.key())
            widget->setFont(it.value());
    }
    m_searchFonts.clear();
}

void SettingsDialog::applySettingsSearch()
{
    if (!m_sidebar || !m_stack || !m_search)
        return;
    const QString query = m_search->text();
    clearSettingsSearchHighlight();
    if (query.trimmed().isEmpty()) {
        m_searching = false;
        for (int i = 0; i < m_sidebar->count(); ++i)
            m_sidebar->setRowHidden(i, false);
        m_sidebar->setCurrentRow(qBound(0, m_searchRestoreRow, m_sidebar->count() - 1));
        if (m_searchCount)
            m_searchCount->clear();
        return;
    }
    if (!m_searching) {
        m_searchRestoreRow = m_sidebar->currentRow();
        m_searching = true;
    }
    // Per-sidebar-row texts: the translated row label plus every harvested knob
    // text of the mapped page (group headers carry only their label), so
    // filtering follows translations with no keyword table.
    const QVector<SettingsStructure::SidebarRow> plan = SettingsStructure::sidebarRows();
    QVector<QStringList> rowTexts;
    rowTexts.reserve(plan.size());
    for (int r = 0; r < plan.size(); ++r) {
        QStringList texts;
        if (r < m_sidebar->count()) {
            if (QListWidgetItem *item = m_sidebar->item(r))
                texts.append(item->text());
        }
        const int stackIndex = r < m_sidebarToStack.size() ? m_sidebarToStack.at(r) : -1;
        if (stackIndex >= 0 && stackIndex < m_stack->count())
            texts += UiHelpers::collectSettingTexts(m_stack->widget(stackIndex));
        rowTexts.append(texts);
    }
    const QVector<bool> visible = SettingsStructure::filterSidebarRows(rowTexts, plan, query);
    int shownContent = 0;
    for (int r = 0; r < plan.size() && r < m_sidebar->count(); ++r) {
        const bool show = r < visible.size() && visible.at(r);
        m_sidebar->setRowHidden(r, !show);
        if (show && !plan.at(r).header)
            ++shownContent;
    }
    const int first = SettingsStructure::firstContentRow(visible, plan);
    if (first >= 0 && m_sidebar->isRowHidden(m_sidebar->currentRow()))
        m_sidebar->setCurrentRow(first);
    // Bold the matching knobs on the now-visible page. Bold changes no color,
    // so every scheme keeps its contrast floors by construction.
    if (QWidget *page = m_stack->currentWidget()) {
        const QList<QWidget *> widgets = page->findChildren<QWidget *>();
        for (QWidget *widget : widgets) {
            const bool labelLike = qobject_cast<QLabel *>(widget) != nullptr
                || qobject_cast<QAbstractButton *>(widget) != nullptr
                || qobject_cast<QGroupBox *>(widget) != nullptr;
            if (!labelLike)
                continue;
            QStringList own;
            if (auto *label = qobject_cast<QLabel *>(widget))
                own.append(label->text());
            else if (auto *button = qobject_cast<QAbstractButton *>(widget))
                own << button->text() << button->toolTip();
            else if (auto *box = qobject_cast<QGroupBox *>(widget))
                own << box->title() << box->toolTip();
            if (!UiHelpers::settingQueryMatches(own, query))
                continue;
            if (!m_searchFonts.contains(widget))
                m_searchFonts.insert(widget, widget->font());
            QFont bold = widget->font();
            bold.setWeight(QFont::DemiBold);
            widget->setFont(bold);
        }
    }
    if (m_searchCount) {
        if (shownContent == 0)
            m_searchCount->setText(tr("No matching settings"));
        else {
            int contentRows = 0;
            for (const SettingsStructure::SidebarRow &row : plan) {
                if (!row.header)
                    ++contentRows;
            }
            m_searchCount->setText(tr("%1 of %2 pages").arg(shownContent).arg(contentRows));
        }
    }
}

QWidget *SettingsDialog::buildAutomationPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *transBox = new QGroupBox(tr("Transforms (local, chainable)"), page);
    auto *transLayout = new QVBoxLayout(transBox);
    m_transformStatus = new QLabel(tr("Loading…"), transBox);
    m_transformStatus->setWordWrap(true);
    m_transformStatus->setTextFormat(Qt::RichText);
    transLayout->addWidget(m_transformStatus);
    m_transformList = new QListWidget(transBox);
    m_transformList->setMaximumHeight(160);
    transLayout->addWidget(m_transformList);
    transLayout->addWidget(makeHint(tr("Uncheck to hide from palette & Transform ▾ menu. Built-ins: <code>trim, uppercase, lowercase, capitalize, reverse, base64, url, json-pretty/minify, html-escape, sort-lines, unique-lines, remove-empty-lines, trim-lines</code>. Chainable via preview <i>Transform ▾ → Chain…</i> or palette <code>&gt;transform</code>."), transBox));
    layout->addWidget(transBox);

    auto *snippetBox = new QGroupBox(tr("Snippets (templates)"), page);
    auto *snippetLayout = new QVBoxLayout(snippetBox);
    m_snippetStatus = new QLabel(tr("Loading…"), snippetBox);
    m_snippetStatus->setWordWrap(true);
    m_snippetStatus->setTextFormat(Qt::RichText);
    snippetLayout->addWidget(m_snippetStatus);
    m_snippetList = new QListWidget(snippetBox);
    m_snippetList->setMaximumHeight(120);
    snippetLayout->addWidget(m_snippetList);
    m_snippetShortcutProblems = new QLabel(snippetBox);
    m_snippetShortcutProblems->setWordWrap(true);
    m_snippetShortcutProblems->setTextFormat(Qt::RichText);
    m_snippetShortcutProblems->setStyleSheet(UiHelpers::warningStyle());
    m_snippetShortcutProblems->setVisible(false);
    snippetLayout->addWidget(m_snippetShortcutProblems);
    snippetLayout->addWidget(makeHint(tr("Placeholders: <code>{{clipboard}}</code> / <code>{{text}}</code>, <code>{{date}}</code> YYYY-MM-DD, <code>{{time}}</code> HH:mm, <code>{{datetime}}</code>, <code>{{timestamp}}</code>. Expand via palette <code>&gt;snippet</code>, context menu, or a global shortcut set in Tools ▸ Snippets."), snippetBox));
    auto *snippetRow = new QHBoxLayout();
    auto *addSnippetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add"), snippetBox);
    auto *editSnippetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), tr("Edit"), snippetBox);
    auto *delSnippetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove"), snippetBox);
    snippetRow->addWidget(addSnippetBtn);
    snippetRow->addWidget(editSnippetBtn);
    snippetRow->addWidget(delSnippetBtn);
    snippetRow->addStretch(1);
    snippetLayout->addLayout(snippetRow);
    connect(addSnippetBtn, &QPushButton::clicked, this, [this]{
        // simple inline add
        bool ok=false;
        QString name = QInputDialog::getText(this, tr("New snippet"), tr("Name:"), QLineEdit::Normal, {}, &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        QString tmpl = QInputDialog::getText(this, tr("Template"), tr("Template (use {{clipboard}}, {{date}}…):"), QLineEdit::Normal, QStringLiteral("{{clipboard}}"), &ok);
        if (!ok) return;
        m_ctx.snippets()->createSnippet(name, tmpl, {});
        populateSnippetList(); refreshDiagnostics();
    });
    connect(editSnippetBtn, &QPushButton::clicked, this, [this]{
        auto *it = m_snippetList->currentItem();
        if (!it) return;
        qint64 id = it->data(Qt::UserRole).toLongLong();
        auto opt = m_ctx.snippets()->snippet(id);
        if (!opt) return;
        const Snippet &sn = *opt;
        bool ok=false;
        QString tmpl = QInputDialog::getText(this, tr("Edit snippet"), tr("Template:"), QLineEdit::Normal, sn.templateText, &ok);
        if (!ok) return;
        // Keep the snippet's global shortcut: this inline editor only edits
        // the template (shortcuts are set in Tools ▸ Snippets).
        m_ctx.snippets()->updateSnippet(id, sn.name, tmpl, sn.shortcut);
        populateSnippetList();
    });
    connect(delSnippetBtn, &QPushButton::clicked, this, [this]{
        auto *it = m_snippetList->currentItem();
        if (!it) return;
        qint64 id = it->data(Qt::UserRole).toLongLong();
        if (QMessageBox::question(this, tr("Remove snippet"), tr("Delete \"%1\"?").arg(it->text()))==QMessageBox::Yes) {
            m_ctx.snippets()->deleteSnippet(id);
            populateSnippetList(); refreshDiagnostics();
        }
    });
    layout->addWidget(snippetBox);

    auto *scriptBox = new QGroupBox(tr("Script Actions (QJSEngine sandbox)"), page);
    auto *scriptLayout = new QVBoxLayout(scriptBox);
    m_scriptStatus = new QLabel(tr("Checking…"), scriptBox);
    m_scriptStatus->setWordWrap(true);
    m_scriptStatus->setTextFormat(Qt::RichText);
    scriptLayout->addWidget(m_scriptStatus);
    m_scriptList = new QListWidget(scriptBox);
    m_scriptList->setMaximumHeight(120);
    scriptLayout->addWidget(m_scriptList);
    scriptLayout->addWidget(makeHint(tr("JS files in <code>~/.local/share/egoboard/actions/*.js</code> — each must define <code>function transform(text){ return ...; }</code> and optional <code>var meta = { label: \"Name\", match: \"regex\" }</code>. No file/network globals, 256 kB input cap, 64 kB file cap. Timeout 2 s."), scriptBox));
    auto *scriptRow = new QHBoxLayout();
    auto *openFolderBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("folder")), tr("Open actions folder"), scriptBox);
    auto *reloadBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Reload"), scriptBox);
    auto *exampleBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-new")), tr("Create example"), scriptBox);
    scriptRow->addWidget(openFolderBtn);
    scriptRow->addWidget(reloadBtn);
    scriptRow->addWidget(exampleBtn);
    scriptRow->addStretch(1);
    scriptLayout->addLayout(scriptRow);
    connect(openFolderBtn, &QPushButton::clicked, this, []{
        const QString dir = ScriptActionManager::actionsDir();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(reloadBtn, &QPushButton::clicked, this, [this]{
        if (m_ctx.scripts()) m_ctx.scripts()->reload();
        populateScriptList(); refreshDiagnostics();
    });
    connect(exampleBtn, &QPushButton::clicked, this, [this]{
        const QString dir = ScriptActionManager::actionsDir();
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/example-pretty-json.js");
        if (QFile::exists(path)) {
            QMessageBox::information(this, tr("Script"), tr("Example already exists at %1").arg(path));
            return;
        }
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(ScriptActionManager::exampleSource().toUtf8());
            f.close();
            if (m_ctx.scripts()) m_ctx.scripts()->reload();
            QMessageBox::information(this, tr("Script"), tr("Created %1 — edit it and press Reload.").arg(path));
            populateScriptList(); refreshDiagnostics();
        } else {
            QMessageBox::warning(this, tr("Script"), tr("Cannot write %1").arg(path));
        }
    });
    connect(m_scriptList, &QListWidget::itemChanged, this, [this](QListWidgetItem *it){
        if (!it) return;
        if (m_populatingLists)
            return; // echo of our own clear/insert — not a user toggle
        QString id = it->data(Qt::UserRole).toString();
        bool enabled = it->checkState()==Qt::Checked;
        m_ctx.settings()->setScriptDisabled(id, !enabled);
        scheduleDiagnosticsRefresh();
    });

    scriptLayout->addWidget(makeHint(tr("D-Bus: <code>org.egoboard.Egoboard</code> at <code>/org/egoboard/Egoboard</code> — <code>Search(query, limit)</code> for future KRunner plugin. Try: <code>qdbus org.egoboard.Egoboard /org/egoboard/Egoboard org.egoboard.Egoboard.Search hello 5</code>. Local session bus only, no network."), scriptBox));

    layout->addWidget(scriptBox);

    auto *automationResetRow = new QHBoxLayout();
    auto *automationResetBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                               tr("Reset this page to defaults"), page);
    automationResetBtn->setToolTip(tr("Shows all transforms and re-enables all script actions. Snippets and script files are kept."));
    automationResetBtn->setAccessibleName(tr("Reset Automation page to defaults"));
    automationResetBtn->setAccessibleDescription(
        tr("Shows all transforms and re-enables all script actions."));
    connect(automationResetBtn, &QPushButton::clicked, this, [this] {
        resetPageToDefaults(SettingsManager::SettingsPage::Automation);
    });
    automationResetRow->addWidget(automationResetBtn);
    automationResetRow->addStretch(1);
    layout->addLayout(automationResetRow);

    layout->addStretch(1);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QWidget *SettingsDialog::buildHotkeysPage()
{
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

    auto *deleteKey = new KKeySequenceWidget(hotkeyBox);
    deleteKey->setKeySequence(
        KGlobalAccel::self()->shortcut(m_ctx.hotkeys()->deleteLastAction()).value(0));
    connect(deleteKey, &KKeySequenceWidget::keySequenceChanged, this,
            [this, deleteKey](const QKeySequence &sequence) {
                KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->deleteLastAction(),
                                                  {sequence}, KGlobalAccel::NoAutoloading);
            });
    hotkeyForm->addRow(tr("Delete last entry:"), deleteKey);

    auto *pauseKey = new KKeySequenceWidget(hotkeyBox);
    pauseKey->setKeySequence(
        KGlobalAccel::self()->shortcut(m_ctx.hotkeys()->pauseAction()).value(0));
    connect(pauseKey, &KKeySequenceWidget::keySequenceChanged, this,
            [this, pauseKey](const QKeySequence &sequence) {
                KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->pauseAction(), {sequence},
                                                  KGlobalAccel::NoAutoloading);
            });
    hotkeyForm->addRow(tr("Pause/resume capture:"), pauseKey);

    auto *paletteKey = new QLabel(QStringLiteral("Ctrl+K"), hotkeyBox);
    paletteKey->setTextInteractionFlags(Qt::TextSelectableByMouse);
    paletteKey->setStyleSheet(QStringLiteral("font-family: monospace; background: palette(midlight); padding: 2px 6px; border-radius: 4px;"));
    hotkeyForm->addRow(tr("Command palette (in-app):"), paletteKey);

    auto *resetKeys = new QPushButton(tr("Reset to defaults (Meta+V quick paste / Meta+Shift+V window / Meta+Shift+D / Meta+Shift+P pause)"), hotkeyBox);
    connect(resetKeys, &QPushButton::clicked, this, [this, toggleKey, quickKey, deleteKey, pauseKey] {
        KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->toggleAction(),
                                          HotkeyManager::defaultToggleShortcut());
        KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->quickPasteAction(),
                                          HotkeyManager::defaultQuickPasteShortcut());
        KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->deleteLastAction(),
                                          HotkeyManager::defaultDeleteLastShortcut());
        KGlobalAccel::self()->setShortcut(m_ctx.hotkeys()->pauseAction(),
                                          HotkeyManager::defaultPauseShortcut());
        toggleKey->setKeySequence(HotkeyManager::defaultToggleShortcut().value(0));
        quickKey->setKeySequence(HotkeyManager::defaultQuickPasteShortcut().value(0));
        deleteKey->setKeySequence(HotkeyManager::defaultDeleteLastShortcut().value(0));
        pauseKey->setKeySequence(HotkeyManager::defaultPauseShortcut().value(0));
    });
    hotkeyForm->addRow(QString(), resetKeys);
    auto *hotkeyHint = makeHint(tr("Shortcuts are registered with KWin via KGlobalAccel. They work even when Egoboard is hidden. The palette (Ctrl+K) is local to the window and needs no registration."), hotkeyBox);
    hotkeyForm->addRow(QString(), hotkeyHint);
    hotkeyLayout->addWidget(hotkeyBox);
    hotkeyLayout->addStretch(1);
    return makeScrollable(hotkeyPage); // scrolls like every other page
}

// --- Platform & diagnostics (read-only status + bug report helper) -----------
QWidget *SettingsDialog::buildPlatformDiagnosticsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *platBox = new QGroupBox(tr("Platform integration"), page);
    auto *platLayout = new QVBoxLayout(platBox);
    m_platformStatus = makeStatusPanel(LayerShellHelper::diagnostics(), platBox);
    platLayout->addWidget(new QLabel(tr("<b>Layer-shell (quick-paste overlay)</b>"), platBox));
    platLayout->addWidget(m_platformStatus);

    m_dataControlStatus = makeStatusPanel(QString(), platBox);
    if (m_ctx.dataControl())
        m_dataControlStatus->setText(m_ctx.dataControl()->diagnostics());
    else
        m_dataControlStatus->setText(WlrDataControlHelper::isWayland()
                                         ? tr("wlr-data-control: <b>inactive</b> (no helper)")
                                         : tr("wlr-data-control: <b>n/a</b>"));
    platLayout->addWidget(new QLabel(tr("<b>wlr-data-control (privileged clipboard observe)</b>"), platBox));
    platLayout->addWidget(m_dataControlStatus);

    m_platformDetails = makeHint(QString(), platBox);
    // details filled in refreshDiagnostics
    platLayout->addWidget(m_platformDetails);
    platLayout->addWidget(makeHint(tr("Tips: On Wayland, layer-shell gives the quick-paste popup an exclusive keyboard grab even without focus. wlr-data-control is privileged — if KWin does not expose <code>zwlr_data_control_manager_v1</code> or denies permission, Egoboard falls back to <code>QClipboard</code> polling (focus-based). Set <code>WAYLAND_DEBUG=1</code> to see protocol traffic."), platBox));
    layout->addWidget(platBox);

    auto *diagBox = new QGroupBox(tr("Diagnostics report"), page);
    auto *diagLayout = new QVBoxLayout(diagBox);
    diagLayout->addWidget(makeHint(tr("Copy-paste this for bug reports — no sensitive content, just local config & counts. All data stays on disk."), diagBox));
    m_diagBrowser = new QTextBrowser(diagBox);
    m_diagBrowser->setReadOnly(true);
    m_diagBrowser->setOpenExternalLinks(false);
    m_diagBrowser->setStyleSheet(QStringLiteral("font-family: monospace;")); // size follows the UI font (U5)
    diagLayout->addWidget(m_diagBrowser, 1);
    auto *row = new QHBoxLayout();
    auto *copyBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy to clipboard"), diagBox);
    auto *refreshBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Refresh"), diagBox);
    row->addWidget(copyBtn);
    row->addWidget(refreshBtn);
    row->addStretch(1);
    diagLayout->addLayout(row);
    connect(copyBtn, &QPushButton::clicked, this, [this]{ if(m_diagBrowser) QGuiApplication::clipboard()->setText(m_diagBrowser->toPlainText()); });
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsDialog::refreshDiagnostics);
    layout->addWidget(diagBox);
    auto *crashRow = new QHBoxLayout();
    auto *createReportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                            tr("Create crash report…"), page);
    createReportBtn->setToolTip(tr("Collects a local support bundle (versions, session, logs, "
                                   "tool status) and previews it before saving — same schema as "
                                   "`--crash-report`. Never includes clipboard entries, history.db "
                                   "or keys."));
    auto *openReportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")),
                                          tr("Open crash report…"), page);
    openReportBtn->setToolTip(tr("Reads a saved report back (same renderer as "
                                 "`--read-crash-report`); partial files render safely."));
    connect(createReportBtn, &QPushButton::clicked, this, &SettingsDialog::createCrashReport);
    connect(openReportBtn, &QPushButton::clicked, this, &SettingsDialog::openCrashReport);
    crashRow->addWidget(createReportBtn);
    crashRow->addWidget(openReportBtn);
    crashRow->addStretch(1);
    layout->addLayout(crashRow);
    return makeScrollable(page); // scrolls like every other page (U14 narrow)
}

QWidget *SettingsDialog::buildAboutPage()
{
    // Sade About: version, license, local-only note. No knobs, so no
    // load()/save()/reset wiring (like Usage and Diagnostics).
    const SettingsStructure::AboutInfo info =
        SettingsStructure::aboutInfo(QStringLiteral(EGOBOARD_VERSION));
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setAlignment(Qt::AlignTop);

    auto *icon = new QLabel(page);
    const QIcon theme = QIcon::fromTheme(QStringLiteral("egoboard"),
                                         QIcon(QStringLiteral(":/icons/egoboard.svg")));
    if (!theme.isNull())
        icon->setPixmap(theme.pixmap(DesignTokens::IconL * 3, DesignTokens::IconL * 3));
    icon->setAlignment(Qt::AlignCenter);
    icon->setAccessibleName(info.title);
    layout->addWidget(icon);

    auto *title = new QLabel(info.title, page);
    QFont titleFont = title->font();
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setPointSize(titleFont.pointSize() + 4);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    title->setTextFormat(Qt::PlainText);
    layout->addWidget(title);

    auto *version = new QLabel(info.version, page);
    version->setAlignment(Qt::AlignCenter);
    version->setTextFormat(Qt::PlainText);
    version->setAccessibleName(tr("Application version"));
    layout->addWidget(version);

    for (const QString &paragraph : info.paragraphs) {
        QLabel *line = makeHint(paragraph, page, /*richText=*/false);
        line->setAlignment(Qt::AlignCenter);
        layout->addWidget(line);
    }
    layout->addStretch(1);
    return makeScrollable(page); // scrolls like every other page (U14 narrow)
}

void SettingsDialog::populateTransformList()
{
    if (!m_transformList) return;
    QSignalBlocker transformSignals(m_transformList); // insertion emits itemChanged
    m_populatingLists = true;
    m_transformList->clear();
    const auto descs = TransformEngine::allDescriptors();
    const QStringList hidden = m_ctx.settings()->hiddenTransforms();
    for (const auto &d : descs) {
        auto *it = new QListWidgetItem(d.label, m_transformList);
        it->setData(Qt::UserRole, d.name);
        it->setToolTip(d.description + QStringLiteral("  (") + d.name + QStringLiteral(")"));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(hidden.contains(d.name, Qt::CaseInsensitive) ? Qt::Unchecked : Qt::Checked);
    }
    // disconnect previous to avoid loop
    disconnect(m_transformList, &QListWidget::itemChanged, nullptr, nullptr);
    connect(m_transformList, &QListWidget::itemChanged, this, [this](QListWidgetItem *it){
        Q_UNUSED(it);
        if (m_populatingLists)
            return; // echo of our own clear/insert — not a user toggle
        QStringList hidden;
        for (int i=0;i<m_transformList->count();++i) {
            auto *item = m_transformList->item(i);
            if (item->checkState()==Qt::Unchecked) hidden << item->data(Qt::UserRole).toString();
        }
        m_ctx.settings()->setHiddenTransforms(hidden);
        scheduleDiagnosticsRefresh();
    });
    m_populatingLists = false;
}

void SettingsDialog::populateSnippetList()
{
    if (!m_snippetList || !m_ctx.snippets()) return;
    m_snippetList->clear();
    const auto sns = m_ctx.snippets()->snippets();
    for (const auto &s : sns) {
        const QString shortcut = s.shortcut.trimmed().isEmpty()
            ? QString()
            : QStringLiteral("  [%1]").arg(QKeySequence::fromString(s.shortcut, QKeySequence::PortableText)
                                               .toString(QKeySequence::NativeText));
        auto *it = new QListWidgetItem(QStringLiteral("%1%2 — %3").arg(s.name, shortcut, s.templateText.left(40)), m_snippetList);
        it->setData(Qt::UserRole, s.id);
        it->setToolTip(s.templateText);
    }
    // Snippet shortcuts that could not be bound (invalid, reserved, duplicated)
    // are reported instead of failing silently.
    if (m_snippetShortcutProblems) {
        const QStringList problems = m_ctx.snippetShortcutProblems();
        m_snippetShortcutProblems->setVisible(!problems.isEmpty());
        if (!problems.isEmpty()) {
            m_snippetShortcutProblems->setText(tr("<b>Unbound snippet shortcuts:</b><br>%1")
                                                   .arg(problems.join(QStringLiteral("<br>"))));
        }
    }
}

void SettingsDialog::populateScriptList()
{
    if (!m_scriptList) return;
    QSignalBlocker scriptSignals(m_scriptList); // insertion emits itemChanged
    m_populatingLists = true;
    m_scriptList->clear();
    if (!m_ctx.scripts()) {
        m_populatingLists = false;
        return;
    }
    const auto acts = m_ctx.scripts()->actions();
    const QStringList disabled = m_ctx.settings()->disabledScripts();
    for (const auto &a : acts) {
        auto *it = new QListWidgetItem(QStringLiteral("%1 — %2").arg(a.label, a.filePath), m_scriptList);
        it->setData(Qt::UserRole, a.id);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(disabled.contains(a.id) ? Qt::Unchecked : Qt::Checked);
    }
    if (acts.isEmpty()) {
        m_scriptList->addItem(tr("(no scripts)"));
        m_scriptList->setEnabled(false);
    } else {
        m_scriptList->setEnabled(true);
    }
    m_populatingLists = false;
}

void SettingsDialog::refreshDiagnostics()
{
    if (!m_ftsStatus) return;
    QSqlDatabase db = m_ctx.storage()->database();
    // FTS count — fast, keep synchronous (tiny query)
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
        const auto stats = m_ctx.storage()->stats();
        if (!avail) {
            m_ocrStatus->setText(tr("<b>tesseract not found</b> — install <code>tesseract</code> + <code>tesseract-data-eng</code> to enable image search. Preview will show <i>OCR: processing…</i> until then."));
        } else {
            // Show immediately without version probe (non-blocking), then fetch version async.
            // U19: snapshot the DB values on the owning (GUI) thread first — the
            // worker below only runs the external tesseract probe and never
            // touches StorageManager/SettingsManager off-thread.
            m_ocrStatus->setText(tr("<b>tesseract OK</b> — checking version… — <b>%1</b> images, <b>%2</b> with OCR text. Lang: <b>%3</b>").arg(stats.imageCount).arg(stats.ocrCount).arg(m_ctx.settings()->ocrLanguage()));
            const StorageStats ocrStatsSnap = stats;
            const QString ocrLangSnap = m_ctx.settings()->ocrLanguage();
            QPointer<SettingsDialog> guard(this);
            QThreadPool::globalInstance()->start([guard, ocrStatsSnap, ocrLangSnap]{
                const QString ver = tesseractVersion();
                QMetaObject::invokeMethod(qApp, [guard, ver, ocrStatsSnap, ocrLangSnap]{
                    if (!guard) return;
                    if (!guard->m_ocrStatus) return;
                    guard->m_ocrStatus->setText(tr("<b>tesseract OK</b> — %1 — <b>%2</b> images, <b>%3</b> with OCR text. Lang: <b>%4</b>").arg(ver.isEmpty() ? QStringLiteral("found in PATH") : ver).arg(ocrStatsSnap.imageCount).arg(ocrStatsSnap.ocrCount).arg(ocrLangSnap));
                }, Qt::QueuedConnection);
            });
        }
    }
    if (m_transformStatus) {
        const int count = TransformEngine::allDescriptors().size();
        const int hidden = m_ctx.settings()->hiddenTransforms().size();
        m_transformStatus->setText(tr("<b>%1</b> built-in transforms (%2 hidden) — chainable in preview and palette. Scripts extend this list.").arg(count).arg(hidden));
    }
    if (m_snippetStatus) {
        const int count = m_ctx.snippets() ? m_ctx.snippets()->snippets().size() : 0;
        m_snippetStatus->setText(tr("<b>%1</b> snippet(s) stored locally — DB table <code>snippets</code>. Use <i>Add / Edit / Remove</i> above or palette <code>&gt;snippet</code>.").arg(count));
    }
    if (m_scriptStatus) {
        const QString dir = ScriptActionManager::actionsDir();
        int sc = 0;
        QStringList names;
        if (m_ctx.scripts()) {
            const auto acts = m_ctx.scripts()->actions();
            sc = acts.size();
            for (const auto &a : acts) names << a.label;
        }
        const int disabled = m_ctx.settings()->disabledScripts().size();
        m_scriptStatus->setText(sc > 0
            ? tr("<b>%1</b> script(s) from <code>%2</code>: %3 — <b>%4</b> disabled").arg(sc).arg(dir.toHtmlEscaped(), names.join(QStringLiteral(", ")).toHtmlEscaped()).arg(disabled)
            : tr("No scripts — add <code>*.js</code> to <code>%1</code> and press Reload. Try <i>Create example</i>.").arg(dir.toHtmlEscaped()));
    }
    if (m_platformStatus) {
        m_platformStatus->setText(LayerShellHelper::diagnostics());
    }
    if (m_dataControlStatus) {
        if (m_ctx.dataControl())
            m_dataControlStatus->setText(m_ctx.dataControl()->diagnostics());
        else
            m_dataControlStatus->setText(WlrDataControlHelper::isWayland()
                                             ? tr("wlr-data-control: <b>inactive</b>")
                                             : tr("wlr-data-control: <b>n/a</b>"));
    }
    if (m_encryptionStatus) {
        EncryptionManager enc;
        const QString cipher = m_ctx.storage()->cipherVersion();
        const bool avail = m_ctx.storage()->isSqlCipherAvailable();
        const bool enabled = m_ctx.settings()->encryptionEnabled();
        const bool locked = m_ctx.storage()->requiresEncryptionKey();
        const bool encrypted = m_ctx.storage()->isEncrypted() || locked;
        QString txt = tr("KWallet: %1 · SQLCipher: %2 · cipher: %3 · enabled: %4 · database: %5")
                          .arg(enc.walletStatusText(), avail ? tr("yes") : tr("no"), cipher.isEmpty() ? tr("n/a") : cipher, enabled ? tr("yes") : tr("no"),
                               encrypted ? (locked ? tr("encrypted, locked") : tr("encrypted")) : tr("plaintext"));
        if (enabled && !avail) txt += tr(" — rebuild with -DEGOBOARD_USE_SQLCIPHER=ON + sqlcipher");
        else if (locked && !enabled) txt += tr(" — re-enable encryption to unlock the database");
        m_encryptionStatus->setText(txt);
    }
    if (m_platformDetails) {
        // Show immediately without kwin probe, then fetch async
        QString details = QStringLiteral("QPA: <b>%1</b> · Qt %2<br/>").arg(QGuiApplication::platformName().toHtmlEscaped(), QString::fromUtf8(qVersion()));
        details += QStringLiteral("DB: <code>%1</code>").arg(m_ctx.storage()->databasePath().toHtmlEscaped());
        details += QStringLiteral("<br/><span style='color:palette(mid);'>checking KWin…</span>");
        m_platformDetails->setText(details);
        // U19: snapshot GUI-owned values before dispatching — the worker only
        // runs the external kwin probe and never touches StorageManager off-thread.
        const QString qpaSnap = QGuiApplication::platformName();
        const QString dbPathSnap = m_ctx.storage()->databasePath();
        QPointer<SettingsDialog> guard(this);
        QThreadPool::globalInstance()->start([guard, qpaSnap, dbPathSnap]{
            const QString kw = kwinVersion();
            QMetaObject::invokeMethod(qApp, [guard, kw, qpaSnap, dbPathSnap]{
                if (!guard) return;
                if (!guard->m_platformDetails) return;
                QString d = QStringLiteral("QPA: <b>%1</b> · Qt %2<br/>").arg(qpaSnap.toHtmlEscaped(), QString::fromUtf8(qVersion()));
                if (!kw.isEmpty()) d += QStringLiteral("KWin: %1<br/>").arg(kw.toHtmlEscaped());
                d += QStringLiteral("DB: <code>%1</code>").arg(dbPathSnap.toHtmlEscaped());
                guard->m_platformDetails->setText(d);
            }, Qt::QueuedConnection);
        });
    }
    // storage page size label — fast, keep synchronous
    if (auto *lbl = findChild<QLabel*>(QStringLiteral("dbSizeLabel"))) {
        const qint64 size = m_ctx.storage()->databaseFileSize();
        const auto stats = m_ctx.storage()->stats();
        lbl->setText(tr("<b>%1</b> on disk · <b>%2</b> entries · <b>%3</b> pinned · <b>%4</b> images · <b>%5</b> with OCR").arg(humanSize(size)).arg(stats.entryCount).arg(stats.pinnedCount).arg(stats.imageCount).arg(stats.ocrCount));
    }
    if (auto *lbl = findChild<QLabel*>(QStringLiteral("pragmaLabel"))) {
        QSqlQuery q(db);
        QString pragmas;
        if (q.exec(QStringLiteral("PRAGMA journal_mode")) && q.next()) pragmas += QStringLiteral("journal_mode=%1 ").arg(q.value(0).toString());
        if (q.exec(QStringLiteral("PRAGMA foreign_keys")) && q.next()) pragmas += QStringLiteral("foreign_keys=%1 ").arg(q.value(0).toString());
        if (q.exec(QStringLiteral("PRAGMA page_size")) && q.next()) pragmas += QStringLiteral("page_size=%1").arg(q.value(0).toString());
        lbl->setText(tr("PRAGMA: <code>%1</code>").arg(pragmas.toHtmlEscaped()));
    }
    // diagnostics browser — build without blocking version probes, then patch async
    if (m_diagBrowser) {
        QString diag;
        diag += QStringLiteral("Egoboard %1\n").arg(QStringLiteral(EGOBOARD_VERSION));
        diag += QStringLiteral("QPA: %1 · Qt %2 · KF6 6.0+\n").arg(QGuiApplication::platformName(), QString::fromUtf8(qVersion()));
        diag += QStringLiteral("DB: %1\n").arg(m_ctx.storage()->databasePath());
        const auto stats = m_ctx.storage()->stats();
        diag += QStringLiteral("Entries: %1 pinned:%2 images:%3 ocr:%4\n").arg(stats.entryCount).arg(stats.pinnedCount).arg(stats.imageCount).arg(stats.ocrCount);
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM entries_fts")) && q.next())
            diag += QStringLiteral("FTS rows: %1\n").arg(q.value(0).toLongLong());
        diag += QStringLiteral("OCR: %1 lang=%2 maxChars=%3\n").arg(OcrWorker::isAvailable()?QStringLiteral("available"):QStringLiteral("missing"), m_ctx.settings()->ocrLanguage()).arg(m_ctx.settings()->ocrMaxChars());
        diag += QStringLiteral("Encryption: %1 enabled=%2 sqlcipher=%3 cipher=%4\n").arg(EncryptionManager{}.walletStatusText(), m_ctx.settings()->encryptionEnabled() ? QStringLiteral("yes") : QStringLiteral("no"), m_ctx.storage()->isSqlCipherAvailable() ? QStringLiteral("yes") : QStringLiteral("no"), m_ctx.storage()->cipherVersion());
        diag += QStringLiteral("Preview: codeHighlight=%1 linkify=%2 colorSwatches=%3\n").arg(m_ctx.settings()->previewCodeHighlight() ? QStringLiteral("on") : QStringLiteral("off")).arg(m_ctx.settings()->previewLinkify() ? QStringLiteral("on") : QStringLiteral("off")).arg(m_ctx.settings()->previewColorSwatches() ? QStringLiteral("on") : QStringLiteral("off"));
        // Which theme is stored, which file it comes from, and which icon theme
        // Qt actually has active (the fallback covers icons a theme lacks).
        const QString schemePath = ColorSchemeIndex::filePath(m_ctx.settings()->theme());
        diag += QStringLiteral("Themes: color=%1 source=%2 icon=%3 active=%4 fallback=%5 text=%6 dim=%7 font=%8pt\n")
                    .arg(m_ctx.settings()->theme(),
                         schemePath.isEmpty() ? QStringLiteral("style-palette") : QFileInfo(schemePath).fileName(),
                         m_ctx.settings()->iconTheme(),
                         QIcon::themeName().isEmpty() ? QStringLiteral("none") : QIcon::themeName(),
                         IconThemeIndex::fallbackId().isEmpty() ? QStringLiteral("none") : IconThemeIndex::fallbackId(),
                         m_ctx.settings()->textColor().isEmpty() ? QStringLiteral("theme") : m_ctx.settings()->textColor(),
                         m_ctx.settings()->dimTextColor().isEmpty() ? QStringLiteral("theme") : m_ctx.settings()->dimTextColor())
                    .arg(m_ctx.settings()->fontPointDelta());
        diag += QStringLiteral("Platform: %1\n").arg(LayerShellHelper::diagnostics().remove(QRegularExpression(QStringLiteral("<[^>]*>"))));
        diag += QStringLiteral("DataControl: %1\n").arg(m_ctx.dataControl() ? m_ctx.dataControl()->diagnostics().remove(QRegularExpression(QStringLiteral("<[^>]*>"))) : QStringLiteral("n/a"));
        diag += QStringLiteral("Settings: debounce=%1 quickPaste=%2 maxItem=%3 maxImage=%4 diskCap=%5 maxEntries=%6\n")
                    .arg(m_ctx.settings()->debounceMs()).arg(m_ctx.settings()->quickPasteCount()).arg(m_ctx.settings()->maxItemBytes()).arg(m_ctx.settings()->maxImageBytes()).arg(m_ctx.settings()->diskCapBytes()).arg(m_ctx.settings()->maxEntries());
        diag += QStringLiteral("Autostart: %1 entry=%2 command=%3 chosen=%4\n")
                    .arg(m_ctx.settings()->autostartEnabled() ? QStringLiteral("on")
                                                              : QStringLiteral("off"),
                         SettingsManager::autostartDesktopFilePath(),
                         m_ctx.settings()->autostartEnabled()
                             ? m_ctx.settings()->effectiveAutostartCommand()
                             : QStringLiteral("-"),
                         m_ctx.settings()->autostartCommand().isEmpty()
                             ? QStringLiteral("running-binary")
                             : m_ctx.settings()->autostartCommand());
        diag += QStringLiteral("Capture: text=%1 rich=%2 image=%3 files=%4\n")
                    .arg(m_ctx.settings()->captureText() ? QStringLiteral("on") : QStringLiteral("off"),
                         m_ctx.settings()->captureRichText() ? QStringLiteral("on") : QStringLiteral("off"),
                         m_ctx.settings()->captureImages() ? QStringLiteral("on") : QStringLiteral("off"),
                         m_ctx.settings()->captureFiles() ? QStringLiteral("on") : QStringLiteral("off"));
        diag += QStringLiteral("(versions: fetching tesseract/KWin async…)\n");
        m_diagBrowser->setPlainText(diag);
        // async patch versions. U19: snapshot availability on the GUI thread —
        // the worker only runs the external probes, never QObject state.
        const bool ocrAvailSnap = OcrWorker::isAvailable();
        QPointer<SettingsDialog> guard(this);
        QThreadPool::globalInstance()->start([guard, ocrAvailSnap]{
            const QString kw = kwinVersion();
            const QString tess = ocrAvailSnap ? tesseractVersion() : QString();
            QMetaObject::invokeMethod(qApp, [guard, kw, tess]{
                if (!guard) return;
                if (!guard->m_diagBrowser) return;
                QString cur = guard->m_diagBrowser->toPlainText();
                if (!kw.isEmpty() && !cur.contains(QStringLiteral("KWin:"))) {
                    cur.replace(QStringLiteral("QPA:"), QStringLiteral("KWin: %1\nQPA:").arg(kw));
                }
                if (!tess.isEmpty()) {
                    cur.replace(QStringLiteral("(versions: fetching"), QStringLiteral("tesseract: %1\n(versions: done").arg(tess));
                } else {
                    cur.replace(QStringLiteral("(versions: fetching tesseract/KWin async…)\n"), QString());
                }
                guard->m_diagBrowser->setPlainText(cur);
            }, Qt::QueuedConnection);
        });
    }
    // repopulate lists if needed
    if (m_transformList && m_transformList->count()==0 && !m_populatingLists) populateTransformList();
    if (m_scriptList && m_scriptList->count()==0 && !m_populatingLists) populateScriptList();
}

void SettingsDialog::scheduleDiagnosticsRefresh()
{
    // Coalesce: at most one refresh in flight. Runs from the event loop, so it
    // cannot re-enter a signal handler that is currently emitting.
    QTimer::singleShot(0, this, [this] {
        if (m_populatingLists)
            return; // a rebuild started meanwhile — its caller refreshes after
        refreshDiagnostics();
    });
}

void SettingsDialog::createCrashReport()
{
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Crash report folder"), QDir::tempPath());
    if (dir.isEmpty())
        return;
    QProgressDialog progress(tr("Collecting crash report…"), QString(), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr);
    progress.show();
    QApplication::processEvents();
    CrashReport::Env env;
    env.appVersion = QStringLiteral(EGOBOARD_VERSION);
    env.qpa = QGuiApplication::platformName();
    const CrashReport::Data data = CrashReport::collect(env);
    progress.close();
    previewCrashReport(data, dir);
}

void SettingsDialog::openCrashReport()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open crash report"), QString(), tr("Crash reports (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open crash report"),
                             tr("Cannot read %1: %2").arg(path, file.errorString()));
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Open crash report"),
                             tr("%1 is not a valid report file.").arg(path));
        return;
    }
    CrashReport::Data data;
    QString error;
    if (!CrashReport::fromJson(document.object(), &data, &error)) {
        QMessageBox::warning(this, tr("Open crash report"), error);
        return;
    }
    previewCrashReport(data, QString());
}

void SettingsDialog::previewCrashReport(const CrashReport::Data &data, const QString &saveDir)
{
    QDialog preview(this);
    preview.setWindowTitle(saveDir.isEmpty() ? tr("Crash report") : tr("Crash report preview"));
    preview.resize(700, 560);
    auto *layout = new QVBoxLayout(&preview);
    auto *browser = new QTextBrowser(&preview);
    browser->setReadOnly(true);
    browser->setPlainText(CrashReport::renderSummary(data));
    browser->setStyleSheet(QStringLiteral("font-family: monospace;")); // size follows the UI font (U5)
    layout->addWidget(browser, 1);
    auto *note = new QLabel(saveDir.isEmpty()
            ? tr("Read-only preview — the same renderer the CLI uses. Raw tool output is kept "
                 "in the file for debugging.")
            : tr("Review before saving: versions, session, redacted logs and tool status. No "
                 "clipboard entries, history.db, image blobs, keys or full home paths are "
                 "included; nothing is uploaded."),
        &preview);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(
        saveDir.isEmpty() ? QDialogButtonBox::Close
                          : QDialogButtonBox::Save | QDialogButtonBox::Cancel,
        &preview);
    connect(buttons, &QDialogButtonBox::accepted, &preview, [&preview, &data, saveDir, this] {
        if (saveDir.isEmpty()) {
            preview.accept();
            return;
        }
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        const QString path =
            QDir(saveDir).filePath(QStringLiteral("egoboard-report-%1.json").arg(stamp));
        QFile file(path);
        const QByteArray payload =
            QJsonDocument(CrashReport::toJson(data)).toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(payload) != payload.size()) {
            QMessageBox::warning(this, tr("Crash report"),
                                 tr("Cannot write %1: %2").arg(path, file.errorString()));
            return;
        }
        QMessageBox::information(this, tr("Crash report"),
                                 tr("Report written to %1.").arg(path));
        preview.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &preview, &QDialog::reject);
    layout->addWidget(buttons);
    preview.exec();
}

void SettingsDialog::load()
{
    m_startVisible->setChecked(m_ctx.settings()->startVisible());
    m_hideOnFocusOut->setChecked(m_ctx.settings()->hideOnFocusOut());
    m_primarySelection->setChecked(m_ctx.settings()->monitorPrimarySelection());
    if (m_pauseOnLock) m_pauseOnLock->setChecked(m_ctx.settings()->pauseOnLock());
    m_quickPasteCount->setValue(m_ctx.settings()->quickPasteCount());
    if (m_quickPasteTwoLine)
        m_quickPasteTwoLine->setChecked(m_ctx.settings()->quickPasteTwoLine());
    m_autostart->setChecked(m_ctx.settings()->autostartEnabled());
    if (m_autostartCommand) {
        m_autostartCommand->setText(m_ctx.settings()->effectiveAutostartCommand());
        m_autostartCommand->setEnabled(m_ctx.settings()->autostartEnabled());
    }
    // Same order as buildCapturePage(): Text / RichText / Image / Files.
    const bool captureTypes[] = {
        m_ctx.settings()->captureText(),
        m_ctx.settings()->captureRichText(),
        m_ctx.settings()->captureImages(),
        m_ctx.settings()->captureFiles(),
    };
    for (int i = 0; i < m_captureTypeBoxes.size() && i < 4; ++i)
        m_captureTypeBoxes[i]->setChecked(captureTypes[i]);
    if (m_trayPrimaryClick) {
        const int idx = m_trayPrimaryClick->findData(int(m_ctx.settings()->trayPrimaryClick()));
        if (idx >= 0) m_trayPrimaryClick->setCurrentIndex(idx);
    }
    if (m_traySecondaryClick) {
        const int idx = m_traySecondaryClick->findData(int(m_ctx.settings()->traySecondaryClick()));
        if (idx >= 0) m_traySecondaryClick->setCurrentIndex(idx);
    }
    if (m_trayWheelCycles) m_trayWheelCycles->setChecked(m_ctx.settings()->trayWheelCycles());

    if (m_trayMode) {
        const QString m = m_ctx.settings()->trayMode();
        int idx = m_trayMode->findData(m);
        if (idx>=0) m_trayMode->setCurrentIndex(idx);
    }
    if (m_notifications) m_notifications->setChecked(m_ctx.settings()->notificationsEnabled());

    if (m_captureSound) m_captureSound->setChecked(m_ctx.settings()->captureSoundEnabled());
    if (m_captureNotification) m_captureNotification->setChecked(m_ctx.settings()->captureNotificationEnabled());

    m_debounce->setValue(m_ctx.settings()->debounceMs());
    // Round up: a sub-MB limit (e.g. 512 kB) must not collapse to 0 = "no limit".
    const auto mbCeil = [](qint64 bytes) {
        return bytes <= 0 ? 0 : int((bytes + 1024 * 1024 - 1) / (1024 * 1024));
    };
    m_maxItemMb->setValue(mbCeil(m_ctx.settings()->maxItemBytes()));
    if (m_maxImageMb) m_maxImageMb->setValue(mbCeil(m_ctx.settings()->maxImageBytes()));
    if (m_maxEntries) m_maxEntries->setValue(m_ctx.settings()->maxEntries());
    switch (m_ctx.settings()->sensitiveMode()) {
    case SettingsManager::SensitiveMode::Off:
        m_sensitiveOff->setChecked(true);
        break;
    case SettingsManager::SensitiveMode::Mark:
        m_sensitiveMark->setChecked(true);
        break;
    case SettingsManager::SensitiveMode::Redact:
        m_sensitiveRedact->setChecked(true);
        break;
    case SettingsManager::SensitiveMode::Exclude:
        m_sensitiveExclude->setChecked(true);
        break;
    }
    const QStringList redactKinds = m_ctx.settings()->redactKinds();
    for (QCheckBox *box : m_redactKindBoxes)
        if (box)
            box->setChecked(redactKinds.contains(box->text()));
    updateRedactUi();
    m_expireRules = m_ctx.settings()->expireRules();
    refreshExpireList();
    if (m_customPatterns) m_customPatterns->setPlainText(m_ctx.settings()->customSensitivePatterns().join(QStringLiteral("\n")));
    m_diskCapMb->setValue(mbCeil(m_ctx.settings()->diskCapBytes()));
    if (m_ignoredApps) {
        m_ignoredApps->setPlainText(m_ctx.settings()->ignoredSourceApps().join(QStringLiteral("\n")));
    }
    if (m_ocrEnabled) m_ocrEnabled->setChecked(m_ctx.settings()->ocrEnabled());
    if (m_ocrLang) {
        int idx = m_ocrLang->findText(m_ctx.settings()->ocrLanguage());
        if (idx>=0) m_ocrLang->setCurrentIndex(idx);
        else m_ocrLang->setCurrentText(m_ctx.settings()->ocrLanguage());
    }
    if (m_ocrMaxChars) m_ocrMaxChars->setValue(m_ctx.settings()->ocrMaxChars());
    if (m_encryptionEnabled) m_encryptionEnabled->setChecked(m_ctx.settings()->encryptionEnabled());
    if (m_backupEnabled) m_backupEnabled->setChecked(m_ctx.settings()->backupsEnabled());
    if (m_backupFolder) m_backupFolder->setText(m_ctx.settings()->backupFolder());
    if (m_backupKeep) m_backupKeep->setValue(m_ctx.settings()->backupKeep());
    if (m_backupStatus) {
        const qint64 last = m_ctx.settings()->lastBackupMs();
        m_backupStatus->setText(last > 0
            ? tr("Last backup: %1").arg(QDateTime::fromMSecsSinceEpoch(last).toString(
                  QLocale::system().dateTimeFormat(QLocale::ShortFormat)))
            : tr("No backup has run yet."));
    }
    if (m_previewCode) m_previewCode->setChecked(m_ctx.settings()->previewCodeHighlight());
    if (m_previewLinks) m_previewLinks->setChecked(m_ctx.settings()->previewLinkify());
    if (m_previewColors) m_previewColors->setChecked(m_ctx.settings()->previewColorSwatches());
    if (m_timelineEnabled) m_timelineEnabled->setChecked(m_ctx.settings()->timelineEnabled());
    if (m_densityCombo) {
        const int idx = m_densityCombo->findData(m_ctx.settings()->listDensity());
        if (idx >= 0) m_densityCombo->setCurrentIndex(idx);
    }
    if (m_closeAfterPaste) m_closeAfterPaste->setChecked(m_ctx.settings()->closeAfterPaste());
    if (m_bumpOnPaste) m_bumpOnPaste->setChecked(m_ctx.settings()->bumpOnPaste());
    if (m_pasteAsPlainText) m_pasteAsPlainText->setChecked(m_ctx.settings()->pasteAsPlainText());
    if (m_portalPaste) m_portalPaste->setChecked(m_ctx.settings()->portalPasteEnabled());
    updatePortalStatus();
    if (m_rememberGeometry)
        m_rememberGeometry->setChecked(m_ctx.settings()->rememberWindowGeometry());
    if (m_restoreFilter) m_restoreFilter->setChecked(m_ctx.settings()->restoreLastFilter());
    if (m_timestampCombo) {
        const int idx = m_timestampCombo->findData(m_ctx.settings()->timestampStyle());
        if (idx >= 0) m_timestampCombo->setCurrentIndex(idx);
    }
    if (m_clock24h) m_clock24h->setChecked(m_ctx.settings()->clock24h());
    if (m_themeCombo) {
        // A stored legacy "light"/"dark" id has no combo entry; preselect the
        // scheme it resolves to so the combo shows what is actually applied.
        const QString stored = m_ctx.settings()->theme();
        int idx = m_themeCombo->findData(stored);
        if (idx < 0)
            idx = m_themeCombo->findData(ColorSchemeIndex::resolvedId(stored));
        if (idx >= 0) m_themeCombo->setCurrentIndex(idx);
    }
    if (m_iconThemeCombo) {
        const int idx = m_iconThemeCombo->findData(m_ctx.settings()->iconTheme());
        if (idx >= 0) m_iconThemeCombo->setCurrentIndex(idx);
    }
    // Text readability controls save on change, so loading them must not emit.
    if (m_fontSize) {
        m_fontSize->blockSignals(true);
        m_fontSize->setValue(m_ctx.settings()->fontPointDelta());
        m_fontSize->blockSignals(false);
    }
    // With no stored color the row follows the scheme; the swatch then shows the
    // color the theme currently produces, so picking "Custom" starts from it.
    const auto loadColorRow = [this](QComboBox *combo, KColorButton *button, const QString &stored,
                                     QPalette::ColorRole themeRole) {
        if (!combo || !button)
            return;
        const bool custom = !stored.isEmpty();
        combo->blockSignals(true);
        combo->setCurrentIndex(custom ? 1 : 0);
        combo->blockSignals(false);
        button->setColor(custom ? QColor::fromString(stored) : qApp->palette().color(themeRole));
        button->setEnabled(custom);
    };
    loadColorRow(m_textColorCombo, m_textColorButton, m_ctx.settings()->textColor(), QPalette::Text);
    loadColorRow(m_dimTextColorCombo, m_dimTextColorButton, m_ctx.settings()->dimTextColor(),
                 QPalette::Mid);
    if (m_toolbarIconOnly)
        m_toolbarIconOnly->setChecked(m_ctx.settings()->toolbarIconOnly());
    if (m_reduceMotion)
        m_reduceMotion->setChecked(m_ctx.settings()->reduceMotion());
    if (m_groupByDay)
        m_groupByDay->setChecked(m_ctx.settings()->groupByDay());
    if (m_showEntryIndex)
        m_showEntryIndex->setChecked(m_ctx.settings()->showEntryIndex());
    if (m_showUseCountBadge)
        m_showUseCountBadge->setChecked(m_ctx.settings()->showUseCountBadge());
    if (m_privacyBlur)
        m_privacyBlur->setChecked(m_ctx.settings()->privacyBlur());
}

void SettingsDialog::save()
{
    m_ctx.settings()->setStartVisible(m_startVisible->isChecked());
    m_ctx.settings()->setHideOnFocusOut(m_hideOnFocusOut->isChecked());
    m_ctx.settings()->setMonitorPrimarySelection(m_primarySelection->isChecked());
    if (m_pauseOnLock) m_ctx.settings()->setPauseOnLock(m_pauseOnLock->isChecked());
    m_ctx.settings()->setQuickPasteCount(m_quickPasteCount->value());
    if (m_quickPasteTwoLine)
        m_ctx.settings()->setQuickPasteTwoLine(m_quickPasteTwoLine->isChecked());
    m_ctx.settings()->setAutostartEnabled(m_autostart->isChecked());
    // Same order as buildCapturePage(): Text / RichText / Image / Files.
    if (m_captureTypeBoxes.size() == 4) {
        m_ctx.settings()->setCaptureText(m_captureTypeBoxes[0]->isChecked());
        m_ctx.settings()->setCaptureRichText(m_captureTypeBoxes[1]->isChecked());
        m_ctx.settings()->setCaptureImages(m_captureTypeBoxes[2]->isChecked());
        m_ctx.settings()->setCaptureFiles(m_captureTypeBoxes[3]->isChecked());
    }
    if (m_trayMode) m_ctx.settings()->setTrayMode(m_trayMode->currentData().toString());
    if (m_trayPrimaryClick)
        m_ctx.settings()->setTrayPrimaryClick(
            static_cast<SettingsManager::TrayClick>(m_trayPrimaryClick->currentData().toInt()));
    if (m_traySecondaryClick)
        m_ctx.settings()->setTraySecondaryClick(
            static_cast<SettingsManager::TrayClick>(m_traySecondaryClick->currentData().toInt()));
    if (m_trayWheelCycles) m_ctx.settings()->setTrayWheelCycles(m_trayWheelCycles->isChecked());
    if (m_notifications) m_ctx.settings()->setNotificationsEnabled(m_notifications->isChecked());

    if (m_captureSound) m_ctx.settings()->setCaptureSoundEnabled(m_captureSound->isChecked());
    if (m_captureNotification) m_ctx.settings()->setCaptureNotificationEnabled(m_captureNotification->isChecked());

    m_ctx.settings()->setDebounceMs(m_debounce->value());
    m_ctx.settings()->setMaxItemBytes(qint64(m_maxItemMb->value()) * 1024 * 1024);
    if (m_maxImageMb) m_ctx.settings()->setMaxImageBytes(qint64(m_maxImageMb->value()) * 1024 * 1024);
    if (m_maxEntries) m_ctx.settings()->setMaxEntries(m_maxEntries->value());
    if (m_sensitiveOff->isChecked())
        m_ctx.settings()->setSensitiveMode(SettingsManager::SensitiveMode::Off);
    else if (m_sensitiveMark->isChecked())
        m_ctx.settings()->setSensitiveMode(SettingsManager::SensitiveMode::Mark);
    else if (m_sensitiveRedact->isChecked())
        m_ctx.settings()->setSensitiveMode(SettingsManager::SensitiveMode::Redact);
    else
        m_ctx.settings()->setSensitiveMode(SettingsManager::SensitiveMode::Exclude);
    QStringList redactKinds;
    for (QCheckBox *box : m_redactKindBoxes)
        if (box && box->isChecked())
            redactKinds << box->text();
    m_ctx.settings()->setRedactKinds(redactKinds);
    m_ctx.settings()->setExpireRules(m_expireRules);
    if (m_customPatterns) {
        const QStringList pats = m_customPatterns->toPlainText().split(QRegularExpression(QStringLiteral("[\n,]+")), Qt::SkipEmptyParts);
        m_ctx.settings()->setCustomSensitivePatterns(pats);
    }
    m_ctx.settings()->setDiskCapBytes(qint64(m_diskCapMb->value()) * 1024 * 1024);
    if (m_ignoredApps) {
        const QStringList apps = m_ignoredApps->toPlainText().split(QRegularExpression(QStringLiteral("[\n,]+")), Qt::SkipEmptyParts);
        m_ctx.settings()->setIgnoredSourceApps(apps);
    }
    if (m_ocrEnabled) m_ctx.settings()->setOcrEnabled(m_ocrEnabled->isChecked());
    if (m_ocrLang) m_ctx.settings()->setOcrLanguage(m_ocrLang->currentText());
    if (m_ocrMaxChars) m_ctx.settings()->setOcrMaxChars(m_ocrMaxChars->value());
    if (m_encryptionEnabled) applyEncryptionSetting();
    if (m_backupEnabled) m_ctx.settings()->setBackupsEnabled(m_backupEnabled->isChecked());
    if (m_backupFolder) m_ctx.settings()->setBackupFolder(m_backupFolder->text().trimmed());
    if (m_backupKeep) m_ctx.settings()->setBackupKeep(m_backupKeep->value());
    if (m_previewCode) m_ctx.settings()->setPreviewCodeHighlight(m_previewCode->isChecked());
    if (m_previewLinks) m_ctx.settings()->setPreviewLinkify(m_previewLinks->isChecked());
    if (m_previewColors) m_ctx.settings()->setPreviewColorSwatches(m_previewColors->isChecked());
    if (m_timelineEnabled) m_ctx.settings()->setTimelineEnabled(m_timelineEnabled->isChecked());
    if (m_densityCombo)
        m_ctx.settings()->setListDensity(m_densityCombo->currentData().toString());
    if (m_closeAfterPaste) m_ctx.settings()->setCloseAfterPaste(m_closeAfterPaste->isChecked());
    if (m_bumpOnPaste) m_ctx.settings()->setBumpOnPaste(m_bumpOnPaste->isChecked());
    if (m_pasteAsPlainText) m_ctx.settings()->setPasteAsPlainText(m_pasteAsPlainText->isChecked());
    if (m_portalPaste) m_ctx.settings()->setPortalPasteEnabled(m_portalPaste->isChecked());
    if (m_rememberGeometry)
        m_ctx.settings()->setRememberWindowGeometry(m_rememberGeometry->isChecked());
    if (m_restoreFilter)
        m_ctx.settings()->setRestoreLastFilter(m_restoreFilter->isChecked());
    if (m_timestampCombo)
        m_ctx.settings()->setTimestampStyle(m_timestampCombo->currentData().toString());
    if (m_clock24h) m_ctx.settings()->setClock24h(m_clock24h->isChecked());
    if (m_themeCombo)
        m_ctx.settings()->setTheme(m_themeCombo->currentData().toString());
    if (m_iconThemeCombo)
        m_ctx.settings()->setIconTheme(m_iconThemeCombo->currentData().toString());
    if (m_toolbarIconOnly)
        m_ctx.settings()->setToolbarIconOnly(m_toolbarIconOnly->isChecked());
    if (m_reduceMotion)
        m_ctx.settings()->setReduceMotion(m_reduceMotion->isChecked());
    if (m_groupByDay)
        m_ctx.settings()->setGroupByDay(m_groupByDay->isChecked());
    if (m_showEntryIndex)
        m_ctx.settings()->setShowEntryIndex(m_showEntryIndex->isChecked());
    if (m_showUseCountBadge)
        m_ctx.settings()->setShowUseCountBadge(m_showUseCountBadge->isChecked());
    if (m_privacyBlur)
        m_ctx.settings()->setPrivacyBlur(m_privacyBlur->isChecked());
    // transform/script hidden/disabled are saved immediately on toggle, but also save here
    refreshDiagnostics();
}

void SettingsDialog::resetPageToDefaults(SettingsManager::SettingsPage page)
{
    // The reset persists immediately (same as Import settings): Cancel cannot
    // undo it, but history data and the encryption flag are never touched.
    clearSettingsSearchHighlight();
    m_ctx.settings()->resetPageToDefaults(page);
    load();
    if (page == SettingsManager::SettingsPage::General)
        previewThemes(); // combos reloaded: re-apply the stored pair live
    if (page == SettingsManager::SettingsPage::Automation) {
        populateTransformList();
        populateSnippetList();
        populateScriptList();
    }
    refreshDiagnostics();
    if (m_search && !m_search->text().trimmed().isEmpty())
        applySettingsSearch();
}

void SettingsDialog::refreshProfileList()
{
    if (!m_profileCombo)
        return;
    const QString current = m_profileCombo->currentText();
    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    for (const QString &name : m_ctx.settings()->profileNames())
        m_profileCombo->addItem(name);
    // Keep the active profile selected; otherwise keep the user's typed text.
    const QString active = m_ctx.settings()->activeProfile();
    const int activeRow = m_profileCombo->findText(active, Qt::MatchExactly);
    if (!active.isEmpty() && activeRow >= 0) {
        m_profileCombo->setCurrentIndex(activeRow);
    } else if (!current.trimmed().isEmpty()) {
        m_profileCombo->setCurrentText(current);
    } else {
        m_profileCombo->setCurrentIndex(-1);
    }
    m_profileCombo->blockSignals(false);
}

void SettingsDialog::applyEncryptionSetting()
{
    const bool wanted = m_encryptionEnabled->isChecked();
    if (wanted == m_ctx.settings()->encryptionEnabled())
        return;

    if (!wanted) {
        const bool encryptedOnDisk =
            m_ctx.storage()->isEncrypted() || m_ctx.storage()->requiresEncryptionKey();
        if (encryptedOnDisk
            && QMessageBox::question(
                   this, tr("Encryption"),
                   tr("Decrypt the history database? The file becomes readable without KWallet."))
                   != QMessageBox::Yes) {
            m_encryptionEnabled->setChecked(true);
            return;
        }
        if (encryptedOnDisk && m_ctx.storage()->requiresEncryptionKey()) {
            // The file is locked (key not applied yet): unlock it before the rekey.
            EncryptionManager enc;
            QString key;
            if (enc.readKey(&key) != EncryptionManager::Status::Ok
                || !m_ctx.storage()->setEncryptionKey(key) || !m_ctx.storage()->verifyEncryptionKey()) {
                QMessageBox::warning(this, tr("Encryption"),
                                     tr("The database could not be unlocked for decryption; "
                                        "encryption stays enabled."));
                m_encryptionEnabled->setChecked(true);
                return;
            }
        }
        if (encryptedOnDisk && !m_ctx.storage()->changeEncryptionKey(QString())) {
            QMessageBox::warning(this, tr("Encryption"),
                                 tr("The database could not be decrypted; encryption stays enabled."));
            m_encryptionEnabled->setChecked(true);
            return;
        }
        m_ctx.settings()->setEncryptionEnabled(false);
        return;
    }

    EncryptionManager enc;
    QString key;
    if (enc.readKey(&key) != EncryptionManager::Status::Ok || key.isEmpty()) {
        // Enabling encryption without a stored key: create one on the spot so
        // the database is never left encrypted with a key nobody can find.
        key = EncryptionManager::generateKey();
        if (enc.writeKey(key) != EncryptionManager::Status::Ok) {
            QMessageBox::warning(this, tr("Encryption"),
                                 tr("Could not store a key in KWallet: %1").arg(enc.walletStatusText()));
            m_encryptionEnabled->setChecked(false);
            return;
        }
    }

    // A locked file only needs the key applied; a plaintext one is rekeyed in place.
    const bool ok = m_ctx.storage()->requiresEncryptionKey()
        ? m_ctx.storage()->setEncryptionKey(key) && m_ctx.storage()->verifyEncryptionKey()
        : m_ctx.storage()->changeEncryptionKey(key);
    if (!ok) {
        QMessageBox::warning(this, tr("Encryption"),
                             tr("The database could not be encrypted. A SQLCipher-enabled build is "
                                "required (-DEGOBOARD_USE_SQLCIPHER=ON with the sqlcipher package)."));
        m_encryptionEnabled->setChecked(false);
        return;
    }
    m_ctx.settings()->setEncryptionEnabled(true);
    QMessageBox::information(this, tr("Encryption"),
                             tr("The history database is now encrypted; the key is stored in KWallet."));
}

void SettingsDialog::previewThemes()
{
    if (!m_themeCombo || !m_iconThemeCombo)
        return;
    // Applied but not saved: OK/Apply persists through save(), Cancel puts the
    // stored pair back (see the rejected handler in the constructor).
    m_ctx.applyThemes(m_themeCombo->currentData().toString(),
                      m_iconThemeCombo->currentData().toString(),
                      m_ctx.settings()->textAppearance());
}

void SettingsDialog::updateRedactUi()
{
    if (!m_sensitiveRedact)
        return;
    const bool redact = m_sensitiveRedact->isChecked();
    for (QCheckBox *box : m_redactKindBoxes)
        if (box)
            box->setEnabled(redact);
    if (m_redactTestBtn)
        m_redactTestBtn->setEnabled(redact);
}

void SettingsDialog::updatePortalStatus()
{
    if (!m_portalStatus)
        return;
    // Availability at dialog open: platform first, portal service second.
    // The per-session consent itself happens live at paste time.
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        m_portalStatus->setText(tr("Portal paste is a Wayland feature — this session runs on %1.")
                                    .arg(QGuiApplication::platformName()));
    } else if (PortalPaster::isAvailable()) {
        m_portalStatus->setText(tr("Portal available — opt in above and Plasma will ask permission on first use."));
    } else {
        m_portalStatus->setText(tr("Portal unavailable here — the manual Ctrl+V notification is used."));
    }
}

void SettingsDialog::refreshExpireList()
{
    if (!m_expireList)
        return;
    m_expireList->clear();
    for (const ExpireRule &rule : m_expireRules) {
        QString typeName;
        if (rule.contentType >= 0 && rule.contentType <= int(ContentType::Files))
            typeName = QString::fromLatin1(contentTypeTag(static_cast<ContentType>(rule.contentType)));
        else
            typeName = tr("any");
        const QString app = rule.sourceAppWildcard.isEmpty() ? tr("any app")
                                                              : tr("from %1").arg(rule.sourceAppWildcard);
        QString age;
        if (rule.ageSeconds % 86400 == 0)
            age = tr("%1 d").arg(rule.ageSeconds / 86400);
        else if (rule.ageSeconds % 3600 == 0)
            age = tr("%1 h").arg(rule.ageSeconds / 3600);
        else if (rule.ageSeconds % 60 == 0)
            age = tr("%1 m").arg(rule.ageSeconds / 60);
        else
            age = tr("%1 s").arg(rule.ageSeconds);
        const QString pin = rule.keepPinned ? tr("keep pinned") : tr("pinned removed too");
        auto *item = new QListWidgetItem(tr("%1 · %2 · older than %3 · %4")
                                             .arg(typeName, app, age, pin),
                                         m_expireList);
        item->setData(Qt::UserRole, rule.toString());
    }
}
