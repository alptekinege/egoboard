#include "MainWindow.h"

#include "../ApplicationContext.h"
#include "../AutoPaster.h"
#include "../ScriptActionManager.h"
#include "../SettingsManager.h"
#include "SnippetManager.h"
#include "TransformEngine.h"
#include "BookmarkManager.h"
#include "ClipboardListModel.h"
#include "EntryDelegate.h"
#include "ExportImportDialogs.h"
#include "GroupsDock.h"
#include "PreviewPane.h"
#include "SettingsDialog.h"
#include "SnippetDialog.h"
#include "StorageManager.h"
#include "CommandPalette.h"
#include "TimelineStrip.h"
#include "TransformChainDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <QShortcut>
#include <QToolBar>
#include <QVBoxLayout>

using DateRange = ExportImportDialogs::DateRange;

MainWindow::MainWindow(ApplicationContext &context, QWidget *parent)
    : QMainWindow(parent)
    , m_ctx(context)
{
    setWindowTitle(tr("Egoboard — Clipboard History"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("edit-paste")));
    setAttribute(Qt::WA_QuitOnClose, false); // closing the window keeps the daemon running
    buildUi();
    connectSignals();
    applyCurrentFilter();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    // --- filter bar ---------------------------------------------------------
    auto *filterRow = new QHBoxLayout();

    m_search = new QLineEdit(central);
    m_search->setPlaceholderText(tr("Search history…"));
    m_search->setClearButtonEnabled(true);
    filterRow->addWidget(m_search, 3);

    m_typeCombo = new QComboBox(central);
    m_typeCombo->addItem(tr("All types"), -1);
    m_typeCombo->addItem(tr("Text"), int(ContentType::Text));
    m_typeCombo->addItem(tr("Rich text"), int(ContentType::RichText));
    m_typeCombo->addItem(tr("Images"), int(ContentType::Image));
    m_typeCombo->addItem(tr("Files"), int(ContentType::Files));
    filterRow->addWidget(m_typeCombo);

    m_dateCombo = new QComboBox(central);
    m_dateCombo->addItem(tr("Any time"), 0);
    m_dateCombo->addItem(tr("Today"), 1);
    m_dateCombo->addItem(tr("Yesterday"), 2);
    m_dateCombo->addItem(tr("Past week"), 3);
    m_dateCombo->addItem(tr("Past month"), 4);
    m_dateCombo->addItem(tr("Custom range…"), 99);
    filterRow->addWidget(m_dateCombo);
    m_appCombo = new QComboBox(central);
    m_appCombo->setMinimumWidth(140);
    m_appCombo->addItem(tr("All sources"), QString());
    refreshAppFilter();
    filterRow->addWidget(m_appCombo, 1);

    layout->addLayout(filterRow);

    // Timeline strip: 14-day histogram, click to filter by day
    m_timeline = new TimelineStrip(m_ctx.storage(), central);
    layout->addWidget(m_timeline);

    // --- list + preview -----------------------------------------------------
    auto *splitter = new QSplitter(Qt::Horizontal, central);

    m_model = new ClipboardListModel(m_ctx.storage(), this);
    m_delegate = new EntryDelegate(m_ctx.bookmarks(), this);

    m_list = new QListView(splitter);
    m_list->setModel(m_model);
    m_list->setItemDelegate(m_delegate);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setUniformItemSizes(true);
    m_list->setLayoutMode(QListView::Batched);
    m_list->setBatchSize(50);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    splitter->addWidget(m_list);

    m_preview = new PreviewPane(splitter);
    if (m_ctx.scripts()) m_preview->setScriptManager(m_ctx.scripts());
    m_preview->setSettingsManager(m_ctx.settings());
    splitter->addWidget(m_preview);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 260});
    layout->addWidget(splitter, 1);

    central->setLayout(layout);
    setCentralWidget(central);

    // --- actions ------------------------------------------------------------
    m_toolbar = addToolBar(tr("Toolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setToolButtonStyle(m_ctx.settings()->toolbarIconOnly()
                                      ? Qt::ToolButtonIconOnly
                                      : Qt::ToolButtonTextBesideIcon);

    QAction *pasteNow = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-paste")),
                                            tr("Paste"));
    connect(pasteNow, &QAction::triggered, this, &MainWindow::pasteCurrent);
    m_pasteAction = pasteNow;

    QAction *copyOnly = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                                             tr("Copy only"));
    connect(copyOnly, &QAction::triggered, this, &MainWindow::copyCurrent);
    m_copyAction = copyOnly;

    QAction *pin = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("bookmarks")),
                                        tr("Pin"));
    pin->setCheckable(true);
    connect(pin, &QAction::toggled, this, [this](bool) { togglePinSelected(); });
    m_pinAction = pin;

    QAction *remove = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                           tr("Delete"));
    connect(remove, &QAction::triggered, this, &MainWindow::deleteSelected);
    m_deleteAction = remove;

    m_toolbar->addSeparator();

    m_pinnedOnlyAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("folder-pin")),
                                               tr("Pinned only"));
    m_pinnedOnlyAction->setCheckable(true);
    connect(m_pinnedOnlyAction, &QAction::toggled, this, [this](bool on) {
        FilterSpec filter = m_model->filter();
        filter.pinnedOnly = on;
        m_model->setFilter(filter);
    });

    m_sensitiveAction =
        m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("security-medium")), tr("Audit"));
    m_sensitiveAction->setCheckable(true);
    m_sensitiveAction->setToolTip(tr("Audit view: only sensitive entries"));
    connect(m_sensitiveAction, &QAction::toggled, this, [this](bool on) {
        m_sensitiveAction->setToolTip(
            tr("Audit view: only sensitive entries (%1 flagged)")
                .arg(m_ctx.storage()->stats().sensitiveCount));
        FilterSpec filter = m_model->filter();
        filter.sensitiveOnly = on;
        m_model->setFilter(filter);
    });

    m_deleteFilteredAction =
        m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                             tr("Delete listed"));
    m_deleteFilteredAction->setToolTip(tr("Delete every entry shown by the current filter (ask first)"));
    connect(m_deleteFilteredAction, &QAction::triggered, this, &MainWindow::deleteFiltered);

    m_groupsAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-choose")),
                                        tr("Groups"));
    m_groupsAction->setCheckable(true);

    QAction *paletteAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("system-search")),
                                                 tr("Palette"));
    paletteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    paletteAction->setToolTip(tr("Command palette (Ctrl+K) — fast search & paste"));
    connect(paletteAction, &QAction::triggered, this, &MainWindow::openPalette);

    QAction *snipAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-edit")), tr("Snippets"));
    snipAction->setToolTip(tr("Snippet templates — {{clipboard}}, {{date}} etc. Local only"));
    connect(snipAction, &QAction::triggered, this, &MainWindow::openSnippetDialog);

    QAction *chainAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Chain"));
    chainAction->setToolTip(tr("Transform chain — combine multiple transforms with live preview"));
    connect(chainAction, &QAction::triggered, this, &MainWindow::openTransformChain);

    QAction *settingsAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("configure")),
                                                 tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    QAction *clearAction = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-clear-all")),
                                              tr("Clear"));
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearHistory);

    // --- groups dock --------------------------------------------------------
    m_groupsDock = new GroupsDock(m_ctx.bookmarks(), this);
    addDockWidget(Qt::LeftDockWidgetArea, m_groupsDock);
    m_groupsDock->hide();
    connect(m_groupsAction, &QAction::toggled, m_groupsDock, &QDockWidget::setVisible);
    connect(m_groupsDock, &GroupsDock::groupSelected, this, [this](qint64 groupId) {
        m_groupFilter = groupId;
        applyCurrentFilter();
    });
    connect(m_groupsDock, &GroupsDock::entriesDropped, this,
            [this](const QList<qint64> &entryIds, qint64 groupId) {
                for (const qint64 id : entryIds)
                    m_ctx.bookmarks()->assignEntry(id, groupId);
                m_delegate->clearGroupCache();
            });
    connect(m_ctx.bookmarks(), &BookmarkManager::membershipChanged, m_delegate,
            &EntryDelegate::clearGroupCache);
    connect(m_ctx.bookmarks(), &BookmarkManager::groupsChanged, this, [this] {
        m_delegate->clearGroupCache();
    });

    resize(900, 560);
    updateActionStates();
}

void MainWindow::connectSignals()
{
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(200);
    connect(m_searchDebounce, &QTimer::timeout, this, &MainWindow::applyCurrentFilter);
    // Global palette shortcut (works from list/selection too)
    if (auto *sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this)) {
        connect(sc, &QShortcut::activated, this, &MainWindow::openPalette);
    }
    connect(m_search, &QLineEdit::textChanged, this,
            [this] { m_searchDebounce->start(); });

    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::applyCurrentFilter);
    if (m_timeline) {
        connect(m_timeline, &TimelineStrip::daySelected, this, [this](qint64 from, qint64 to){
            if (from == 0 && to == 0) {
                m_dateCombo->setCurrentIndex(0);
            } else {
                m_lastRange.isValid = true;
                m_lastRange.fromMs = from;
                m_lastRange.toMs = to;
                const int customIdx = m_dateCombo->findData(99);
                if (customIdx >= 0) m_dateCombo->setCurrentIndex(customIdx);
            }
            applyCurrentFilter();
        });
    }
    connect(m_dateCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_dateCombo->currentData().toInt() == 99) {
            ExportImportDialogs::DateRangeDialog dialog(this);
            if (dialog.exec() == QDialog::Accepted) {
                m_lastRange = dialog.range();
                if (!m_lastRange.isValid)
                    m_dateCombo->setCurrentIndex(0);
                else
                    applyCurrentFilter();
            } else {
                m_dateCombo->setCurrentIndex(0); // revert
            }
        } else {
            applyCurrentFilter();
        }
    });
    connect(m_appCombo, &QComboBox::currentIndexChanged, this, &MainWindow::applyCurrentFilter);

    auto selectionModel = m_list->selectionModel();
    connect(selectionModel, &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(m_list, &QListView::activated, this, &MainWindow::onActivated);
    connect(m_list, &QListView::customContextMenuRequested, this, &MainWindow::showContextMenu);

    connect(m_ctx.storage(), &StorageManager::entryAdded, this,
            [this] { refreshAppFilter(); });
    connect(m_ctx.storage(), &StorageManager::storageReset, this, [this] {
        refreshAppFilter();
        m_preview->showEmpty();
    });
    // Live appearance changes (theme is applied globally in ApplicationContext;
    // here we restyle the toolbar button mode).
    connect(m_ctx.settings(), &SettingsManager::changed, this, [this] {
        if (!m_toolbar) return;
        m_toolbar->setToolButtonStyle(m_ctx.settings()->toolbarIconOnly()
                                          ? Qt::ToolButtonIconOnly
                                          : Qt::ToolButtonTextBesideIcon);
    });
}

void MainWindow::refreshAppFilter()
{
    const QString current = m_appCombo->currentData().toString();
    m_appCombo->blockSignals(true);
    m_appCombo->clear();
    m_appCombo->addItem(tr("All sources"), QString());
    const QStringList apps = m_ctx.storage()->sourceApps();
    for (const QString &app : apps)
        m_appCombo->addItem(app, app);
    const int index = m_appCombo->findData(current);
    if (index >= 0)
        m_appCombo->setCurrentIndex(index);
    m_appCombo->blockSignals(false);
}

void MainWindow::applyCurrentFilter()
{
    FilterSpec filter;
    filter.searchText = m_search->text().trimmed();
    filter.contentType = m_typeCombo->currentData().toInt();
    if (m_groupFilter != 0)
        filter.groupId = m_groupFilter;
    if (m_sensitiveAction && m_sensitiveAction->isChecked())
        filter.sensitiveOnly = true;
    if (m_pinnedOnlyAction && m_pinnedOnlyAction->isChecked())
        filter.pinnedOnly = true;

    const int datePreset = m_dateCombo->currentData().toInt();
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime startOfToday(now.date(), QTime(0, 0));
    switch (datePreset) {
    case 1:
        filter.fromMs = startOfToday.toMSecsSinceEpoch();
        break;
    case 2: {
        const QDateTime startOfYesterday(startOfToday.date().addDays(-1), QTime(0, 0));
        filter.fromMs = startOfYesterday.toMSecsSinceEpoch();
        filter.toMs = startOfToday.toMSecsSinceEpoch() - 1;
        break;
    }
    case 3:
        filter.fromMs = now.addDays(-7).toMSecsSinceEpoch();
        break;
    case 4:
        filter.fromMs = now.addMonths(-1).toMSecsSinceEpoch();
        break;
    case 99:
        if (m_lastRange.isValid) {
            filter.fromMs = m_lastRange.fromMs;
            filter.toMs = m_lastRange.toMs;
        }
        break;
    default:
        break;
    }
    filter.sourceApp = m_appCombo->currentData().toString();
    m_model->setFilter(filter);
    if (m_timeline) m_timeline->setFilter(filter);
    updateActionStates();
}

void MainWindow::onSelectionChanged()
{
    const QModelIndexList selected = m_list->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        m_selectedId = 0;
        m_preview->showEmpty();
    } else {
        m_selectedId = selected.first().data(ClipboardListModel::IdRole).toLongLong();
        ClipboardRecord full;
        if (m_ctx.storage()->fetchFull(m_selectedId, &full))
            m_preview->showRecord(full);
    }
    updateActionStates();
    // Reflect pin state in the toolbar toggle.
    if (m_pinAction) {
        QSignalBlocker blocker(m_pinAction);
        m_pinAction->setChecked(selected.size() == 1
                                && selected.first().data(ClipboardListModel::PinnedRole).toBool());
    }
}

void MainWindow::onActivated(const QModelIndex &index)
{
    if (index.isValid())
        pasteEntry(index.data(ClipboardListModel::IdRole).toLongLong());
}

void MainWindow::pasteEntry(qint64 entryId)
{
    m_ctx.pasteEntry(entryId);
}

void MainWindow::pasteCurrent()
{
    if (m_selectedId != 0)
        pasteEntry(m_selectedId);
}

void MainWindow::copyCurrent()
{
    if (m_selectedId == 0)
        return;
    ClipboardRecord record;
    if (!m_ctx.storage()->fetchFull(m_selectedId, &record))
        return;
    m_ctx.autoPaster()->copyToClipboard(record);
}

void MainWindow::deleteSelected()
{
    const QModelIndexList selected = m_list->selectionModel()->selectedIndexes();
    QList<qint64> ids;
    for (const QModelIndex &index : selected)
        ids.append(index.data(ClipboardListModel::IdRole).toLongLong());
    if (ids.isEmpty())
        return;
    m_ctx.storage()->removeEntries(ids);
}

void MainWindow::deleteFiltered()
{
    const FilterSpec filter = m_model->filter();
    const auto all = m_ctx.storage()->fetchAll(filter);
    if (all.isEmpty())
        return;
    QMessageBox box(this);
    box.setWindowTitle(tr("Delete listed entries"));
    box.setText(tr("Delete the %n entry/entries shown by the current filter?", "", all.size()));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;
    QList<qint64> ids;
    ids.reserve(all.size());
    for (const ClipboardRecord &record : all)
        ids.append(record.id);
    m_ctx.storage()->removeEntries(ids);
}

void MainWindow::togglePinSelected()
{
    const QModelIndexList selected = m_list->selectionModel()->selectedIndexes();
    if (selected.size() != 1)
        return;
    const qint64 id = selected.first().data(ClipboardListModel::IdRole).toLongLong();
    const bool pinned = selected.first().data(ClipboardListModel::PinnedRole).toBool();
    m_ctx.storage()->setPinned(id, !pinned);
    QSignalBlocker blocker(m_pinAction);
    m_pinAction->setChecked(!pinned);
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_list->indexAt(pos);
    if (!index.isValid())
        return;
    m_list->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);

    QMenu menu(this);
    QAction *paste = menu.addAction(tr("Paste to window"));
    connect(paste, &QAction::triggered, this, &MainWindow::pasteCurrent);
    QAction *copy = menu.addAction(tr("Copy to clipboard"));
    connect(copy, &QAction::triggered, this, &MainWindow::copyCurrent);

    const bool pinned = index.data(ClipboardListModel::PinnedRole).toBool();
    QAction *pin = menu.addAction(pinned ? tr("Unpin") : tr("Pin"));
    connect(pin, &QAction::triggered, this, &MainWindow::togglePinSelected);

    // Phase 3: Transform submenu (single-step) + chain
    {
        QMenu *tMenu = menu.addMenu(tr("Transform"));
        for (const auto &d : TransformEngine::allDescriptors()) {
            QAction *a = tMenu->addAction(d.label);
            a->setToolTip(d.description);
            connect(a, &QAction::triggered, this, [this, id = d.id, entryId = index.data(ClipboardListModel::IdRole).toLongLong()]{
                ClipboardRecord rec;
                if (!m_ctx.storage()->fetchFull(entryId, &rec)) return;
                QString input = rec.textData.isEmpty() ? rec.preview : rec.textData;
                auto r = TransformEngine::apply(id, input);
                if (!r.ok) {
                    QMessageBox::warning(this, tr("Transform"), r.error);
                    return;
                }
                QGuiApplication::clipboard()->setText(r.output);
                QMessageBox::information(this, tr("Transform"), tr("Applied %1 — copied to clipboard.").arg(TransformEngine::labelForId(id)));
            });
        }
        tMenu->addSeparator();
        if (m_ctx.scripts()) {
            for (const auto &sa : m_ctx.scripts()->actions()) {
                QAction *a = tMenu->addAction(QStringLiteral("[JS] %1").arg(sa.label));
                connect(a, &QAction::triggered, this, [this, sid = sa.id, entryId = index.data(ClipboardListModel::IdRole).toLongLong()]{
                    ClipboardRecord rec;
                    if (!m_ctx.storage()->fetchFull(entryId, &rec)) return;
                    QString input = rec.textData.isEmpty() ? rec.preview : rec.textData;
                    auto r = m_ctx.scripts()->apply(sid, input);
                    if (!r.ok) {
                        QMessageBox::warning(this, tr("Script"), r.error);
                        return;
                    }
                    QGuiApplication::clipboard()->setText(r.output);
                });
            }
        }
        QAction *chain = tMenu->addAction(tr("Chain…"));
        connect(chain, &QAction::triggered, this, &MainWindow::openTransformChain);
    }

    // Phase 3: Snippets submenu — expand with this entry's text
    {
        QMenu *sMenu = menu.addMenu(tr("Insert Snippet"));
        const auto snippets = m_ctx.snippets() ? m_ctx.snippets()->snippets() : QVector<Snippet>{};
        if (snippets.isEmpty()) {
            QAction *a = sMenu->addAction(tr("(no snippets)"));
            a->setEnabled(false);
        } else {
            for (const auto &s : snippets) {
                QAction *a = sMenu->addAction(s.name);
                a->setToolTip(s.templateText);
                connect(a, &QAction::triggered, this, [this, s, entryId = index.data(ClipboardListModel::IdRole).toLongLong()]{
                    ClipboardRecord rec;
                    QString clip;
                    if (m_ctx.storage()->fetchFull(entryId, &rec)) clip = rec.textData.isEmpty() ? rec.preview : rec.textData;
                    const QString out = SnippetManager::expand(s.templateText, clip);
                    QGuiApplication::clipboard()->setText(out);
                });
            }
        }
        sMenu->addSeparator();
        QAction *manage = sMenu->addAction(tr("Manage Snippets…"));
        connect(manage, &QAction::triggered, this, &MainWindow::openSnippetDialog);
    }

    QMenu *groupsMenu = menu.addMenu(tr("Move to group"));
    for (const BookmarkGroup &group : m_ctx.bookmarks()->groups()) {
        QAction *action = groupsMenu->addAction(group.name);
        connect(action, &QAction::triggered, this, [this, id = index.data(ClipboardListModel::IdRole).toLongLong(), groupId = group.id] {
            m_ctx.bookmarks()->assignEntry(id, groupId);
        });
    }
    QAction *unassign = menu.addAction(tr("Remove from all groups"));
    connect(unassign, &QAction::triggered, this, [this, id = index.data(ClipboardListModel::IdRole).toLongLong()] {
        for (const qint64 gid : m_ctx.bookmarks()->groupIdsForEntry(id))
            m_ctx.bookmarks()->removeFromGroup(id, gid);
    });

    menu.addSeparator();
    QAction *remove = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete"));
    connect(remove, &QAction::triggered, this, &MainWindow::deleteSelected);

    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_ctx, this);
    dialog.exec();
    m_delegate->clearGroupCache();
}

void MainWindow::clearHistory()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Clear history"));
    box.setText(tr("Delete the clipboard history?"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    QCheckBox *includePinned = new QCheckBox(tr("Also delete pinned entries"), &box);
    box.setCheckBox(includePinned);
    if (box.exec() != QMessageBox::Yes)
        return;
    const int removed = m_ctx.storage()->clearHistory(includePinned->isChecked());
    QMessageBox::information(this, tr("History cleared"),
                             tr("%n entry(ies) deleted.", nullptr, removed));
}

void MainWindow::updateActionStates()
{
    const bool hasSelection = m_selectedId != 0;
    m_pasteAction->setEnabled(hasSelection);
    m_copyAction->setEnabled(hasSelection);
    m_deleteAction->setEnabled(hasSelection);
}

void MainWindow::toggleVisibility()
{
    if (isVisible()) {
        hide();
    } else {
        repositionCenteredOnActiveScreen();
        show();
        raise();
        activateWindow();
        m_search->setFocus();
        m_search->selectAll();
    }
}

void MainWindow::openPalette()
{
    if (!m_palette) {
        m_palette = new CommandPalette(m_ctx.storage(), this);
        // Wire snippet/script managers for >transform / >snippet commands
        m_palette->setSnippetManager(m_ctx.snippets());
        m_palette->setScriptManager(m_ctx.scripts());
        connect(m_palette, &CommandPalette::pasteRequested, this, &MainWindow::pasteEntry);
        connect(m_palette, &CommandPalette::copyRequested, this, [this](qint64 id){
            ClipboardRecord rec;
            if (m_ctx.storage()->fetchFull(id, &rec))
                m_ctx.autoPaster()->copyToClipboard(rec);
        });
        connect(m_palette, &CommandPalette::transformRequested, this, [this](const QString &name, qint64 entryId){
            // Resolve transform name -> apply
            ClipboardRecord rec;
            QString input;
            if (entryId != 0 && m_ctx.storage()->fetchFull(entryId, &rec))
                input = rec.textData.isEmpty() ? rec.preview : rec.textData;
            else if (m_selectedId != 0 && m_ctx.storage()->fetchFull(m_selectedId, &rec))
                input = rec.textData.isEmpty() ? rec.preview : rec.textData;
            else
                input = QGuiApplication::clipboard()->text();
            if (input.isEmpty()) return;
            // Try builtin first
            if (auto bid = TransformEngine::idForName(name)) {
                auto r = TransformEngine::apply(*bid, input);
                if (r.ok) QGuiApplication::clipboard()->setText(r.output);
                else QMessageBox::warning(this, tr("Transform"), r.error);
                return;
            }
            // Try script
            if (m_ctx.scripts() && m_ctx.scripts()->hasAction(name)) {
                auto r = m_ctx.scripts()->apply(name, input);
                if (r.ok) QGuiApplication::clipboard()->setText(r.output);
                else QMessageBox::warning(this, tr("Script"), r.error);
                return;
            }
            QMessageBox::warning(this, tr("Transform"), tr("Unknown transform: %1").arg(name));
        });
        connect(m_palette, &CommandPalette::snippetRequested, this, [this](qint64 snippetId, qint64 entryId){
            auto s = m_ctx.snippets()->snippet(snippetId);
            if (!s.has_value()) return;
            ClipboardRecord rec;
            QString clip;
            if (entryId != 0 && m_ctx.storage()->fetchFull(entryId, &rec))
                clip = rec.textData.isEmpty() ? rec.preview : rec.textData;
            else if (m_selectedId != 0 && m_ctx.storage()->fetchFull(m_selectedId, &rec))
                clip = rec.textData.isEmpty() ? rec.preview : rec.textData;
            else
                clip = QGuiApplication::clipboard()->text();
            const QString out = SnippetManager::expand(s->templateText, clip);
            QGuiApplication::clipboard()->setText(out);
        });
    }
    m_palette->openPalette();
}

void MainWindow::openSnippetDialog()
{
    QString clip;
    if (m_selectedId != 0) {
        ClipboardRecord rec;
        if (m_ctx.storage()->fetchFull(m_selectedId, &rec))
            clip = rec.textData.isEmpty() ? rec.preview : rec.textData;
    }
    if (clip.isEmpty()) clip = QGuiApplication::clipboard()->text();
    SnippetDialog dlg(m_ctx.snippets(), clip, this);
    connect(&dlg, &SnippetDialog::insertRequested, this, [this](const QString &expanded){
        QGuiApplication::clipboard()->setText(expanded);
        QMessageBox::information(this, tr("Snippet"), tr("Expanded snippet copied to clipboard."));
    });
    dlg.exec();
}

void MainWindow::openTransformChain()
{
    QString input;
    if (m_selectedId != 0) {
        ClipboardRecord rec;
        if (m_ctx.storage()->fetchFull(m_selectedId, &rec))
            input = rec.textData.isEmpty() ? rec.preview : rec.textData;
    }
    if (input.isEmpty()) input = QGuiApplication::clipboard()->text();
    if (input.isEmpty()) {
        QMessageBox::information(this, tr("Transform"), tr("Select an entry or copy text first."));
        return;
    }
    TransformChainDialog dlg(input, m_ctx.scripts(), this);
    if (dlg.exec() == QDialog::Accepted) {
        const QString out = dlg.resultText();
        if (!out.isEmpty()) {
            QGuiApplication::clipboard()->setText(out);
            QMessageBox::information(this, tr("Transform"), tr("Chain result copied to clipboard (%1 chars).").arg(out.size()));
        }
    }
}

void MainWindow::repositionCenteredOnActiveScreen()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect available = screen->availableGeometry();
    move(available.center() - QPoint(width() / 2, height() / 2));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        hide();
        event->accept();
        return;
    case Qt::Key_K: {
        if (event->modifiers() == Qt::ControlModifier) {
            openPalette();
            event->accept();
            return;
        }
        break;
    }
    case Qt::Key_F: {
        if (event->modifiers() == Qt::ControlModifier) {
            m_search->setFocus();
            m_search->selectAll();
            event->accept();
            return;
        }
        break;
    }
    default:
        break;
    }
    // Numeric shortcuts 1-9: paste the Nth visible entry.
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9 && !event->modifiers()) {
        const int row = event->key() - Qt::Key_1;
        if (row < m_model->rowCount()) {
            const qint64 id = m_model->data(m_model->index(row, 0),
                                            ClipboardListModel::IdRole).toLongLong();
            if (id != 0) {
                pasteEntry(id);
                event->accept();
                return;
            }
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // Optional popup-like behaviour: hide when the window loses activation.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() == false
        && m_ctx.settings()->hideOnFocusOut() && isVisible()) {
        // Don't hide while a modal dialog we spawned is working.
        if (QApplication::activeModalWidget() == nullptr
            && QApplication::activePopupWidget() == nullptr)
            hide();
    }
}
