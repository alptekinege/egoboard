#include "MainWindow.h"

#include "../ApplicationContext.h"
#include "../AutoPaster.h"
#include "../ScriptActionManager.h"
#include "../SettingsManager.h"
#include "SnippetManager.h"
#include "TransformEngine.h"
#include "BookmarkManager.h"
#include "ClipboardListModel.h"
#include "DesignTokens.h"
#include "EntryDelegate.h"
#include "ExportImportDialogs.h"
#include "GroupsDock.h"
#include "PreviewPane.h"
#include "SearchEngine.h"
#include "SettingsDialog.h"
#include "SnippetDialog.h"
#include "StorageManager.h"
#include "CommandPalette.h"
#include "TimelineStrip.h"
#include "TransformChainDialog.h"
#include "UiHelpers.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDrag>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <QDockWidget>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QWidgetAction>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <atomic>

using DateRange = ExportImportDialogs::DateRange;

namespace {
// Search scope values come from the settings file as plain ints.
QString scopeLabel(int scope)
{
    switch (static_cast<FilterSpec::SearchScope>(scope)) {
    case FilterSpec::SearchScope::Preview:
        return MainWindow::tr("Preview");
    case FilterSpec::SearchScope::FullText:
        return MainWindow::tr("Full text");
    case FilterSpec::SearchScope::Ocr:
        return MainWindow::tr("OCR");
    case FilterSpec::SearchScope::All:
    default:
        return MainWindow::tr("All text");
    }
}

// The history list keeps the standard drag-out to the Groups dock, but hands
// the drag a compact badge instead of the default full-row snapshot.
class HistoryListView : public QListView {
public:
    using QListView::QListView;

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        const QModelIndexList indexes = selectedIndexes();
        if (indexes.isEmpty())
            return;
        QMimeData *data = model()->mimeData(indexes);
        if (!data)
            return;
        auto *drag = new QDrag(this);
        drag->setMimeData(data);
        const QPixmap badge = dragBadge(indexes.size());
        drag->setPixmap(badge);
        // The hotspot is in logical pixels; the pixmap may be scaled for HiDPI.
        const QSize logical = badge.deviceIndependentSize().toSize();
        drag->setHotSpot(QPoint(logical.width() / 2, logical.height() / 2));
        drag->exec(supportedActions, Qt::CopyAction);
    }

private:
    QPixmap dragBadge(int count) const
    {
        const int size = DesignTokens::IconL + 2 * DesignTokens::SpaceS;
        const qreal ratio = devicePixelRatioF();
        QPixmap pixmap(QSize(size, size) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor background = palette().color(QPalette::Highlight);
        background.setAlpha(230);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(QRect(0, 0, size, size), DesignTokens::RadiusL,
                                DesignTokens::RadiusL);
        painter.setPen(palette().color(QPalette::HighlightedText));
        painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QString::number(count));
        return pixmap;
    }
};
} // namespace

MainWindow::MainWindow(ApplicationContext &context, QWidget *parent)
    : QMainWindow(parent)
    , m_ctx(context)
{
    setWindowTitle(tr("Egoboard — Clipboard History"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("egoboard"),
                                   QIcon(QStringLiteral(":/icons/egoboard.svg"))));
    setAttribute(Qt::WA_QuitOnClose, false); // closing the window keeps the daemon running
    buildUi();
    connectSignals();
    applyCurrentFilter();

    // Session state (both optional): geometry/splitter and the last filter.
    if (m_ctx.settings()->rememberWindowGeometry()) {
        const QByteArray geometry = m_ctx.settings()->windowGeometry();
        if (!geometry.isEmpty())
            restoreGeometry(geometry);
    }
    applyResponsiveMode(width() > 0 ? width() : 900);
    if (m_ctx.settings()->rememberWindowGeometry())
        restoreSplitterForMode();
    if (m_ctx.settings()->restoreLastFilter()) {
        const FilterSpec saved = FilterSpec::fromJsonString(m_ctx.settings()->lastFilter());
        if (!saved.isTrivial())
            applySavedSearch(saved);
    }
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    m_central = central;
    auto *layout = new QVBoxLayout(central);
    m_centralLayout = layout;
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    // --- filter bar: search row + collapsible filter row (U2) ----------------
    m_filterWidget = new QWidget(central);
    auto *filterStack = new QVBoxLayout(m_filterWidget);
    filterStack->setContentsMargins(0, 0, 0, 0);
    filterStack->setSpacing(DesignTokens::SpaceXs);

    auto *searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(0, 0, 0, 0);

    m_search = new QLineEdit(m_filterWidget);
    m_search->setPlaceholderText(tr("Search history…  •  app:firefox  has:ocr  -word  OR  /regex/"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Search history"));
    m_search->setAccessibleDescription(
        tr("Full-text search over previews, stored text and OCR output. Field filters: "
           "app:, type:, tag:, pinned:, sensitive:, has:ocr, before:, after:. Quote phrases; "
           "-term or NOT term excludes; uppercase OR gives alternatives; /pattern/ matches a "
           "regular expression."));
    m_search->setToolTip(m_search->accessibleDescription());
    UiHelpers::styleSearchField(m_search);
    m_search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    searchRow->addWidget(m_search, 3);

    // Trailing actions inside the search field: search scope and recent queries.
    m_searchScope = m_ctx.settings()->searchScope();
    m_scopeAction = m_search->addAction(QIcon::fromTheme(QStringLiteral("edit-find")),
                                        QLineEdit::TrailingPosition);
    m_scopeAction->setToolTip(tr("Search scope: %1").arg(scopeLabel(m_searchScope)));
    connect(m_scopeAction, &QAction::triggered, this, &MainWindow::showScopeMenu);

    m_recentSearchAction = m_search->addAction(QIcon::fromTheme(QStringLiteral("view-history")),
                                               QLineEdit::TrailingPosition);
    m_recentSearchAction->setToolTip(tr("Recent searches"));
    connect(m_recentSearchAction, &QAction::triggered, this, &MainWindow::showRecentSearches);

    // "Filters (n)": collapsed combo row under Medium/Narrow (U2).
    m_filtersButton = new QToolButton(m_filterWidget);
    m_filtersButton->setText(tr("Filters"));
    m_filtersButton->setIcon(QIcon::fromTheme(QStringLiteral("view-filter")));
    m_filtersButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_filtersButton->setPopupMode(QToolButton::InstantPopup);
    m_filtersButton->setAccessibleName(tr("Filter options"));
    m_filtersButton->setVisible(false);
    searchRow->addWidget(m_filtersButton);

    // Saved searches ("smart folders"): apply or store the current filter.
    m_savedSearchesButton = new QToolButton(m_filterWidget);
    m_savedSearchesButton->setText(tr("Searches"));
    m_savedSearchesButton->setIcon(QIcon::fromTheme(QStringLiteral("folder-saved-search")));
    m_savedSearchesButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_savedSearchesButton->setPopupMode(QToolButton::InstantPopup);
    m_savedSearchesButton->setToolTip(tr("Saved searches — apply one, or save the current filter combination."));
    QMenu *searchesMenu = new QMenu(m_savedSearchesButton);
    connect(searchesMenu, &QMenu::aboutToShow, this, &MainWindow::buildSavedSearchesMenu);
    m_savedSearchesButton->setMenu(searchesMenu);
    searchRow->addWidget(m_savedSearchesButton);

    filterStack->addLayout(searchRow);

    m_filterRow = new QWidget(m_filterWidget);
    auto *filterRow = new QHBoxLayout(m_filterRow);
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(DesignTokens::SpaceS);

    const auto comboPolicy = [](QComboBox *combo) {
        combo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        combo->setMinimumContentsLength(8);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    };

    m_typeCombo = new QComboBox(m_filterRow);
    m_typeCombo->setAccessibleName(tr("Content type filter"));
    m_typeCombo->addItem(tr("All types"), -1);
    m_typeCombo->addItem(tr("Text"), int(ContentType::Text));
    m_typeCombo->addItem(tr("Rich text"), int(ContentType::RichText));
    m_typeCombo->addItem(tr("Images"), int(ContentType::Image));
    m_typeCombo->addItem(tr("Files"), int(ContentType::Files));
    comboPolicy(m_typeCombo);
    filterRow->addWidget(m_typeCombo);

    m_dateCombo = new QComboBox(m_filterRow);
    m_dateCombo->setAccessibleName(tr("Date range filter"));
    m_dateCombo->addItem(tr("Any time"), 0);
    m_dateCombo->addItem(tr("Today"), 1);
    m_dateCombo->addItem(tr("Yesterday"), 2);
    m_dateCombo->addItem(tr("Past week"), 3);
    m_dateCombo->addItem(tr("Past month"), 4);
    m_dateCombo->addItem(tr("Custom range…"), 99);
    comboPolicy(m_dateCombo);
    filterRow->addWidget(m_dateCombo);
    m_appCombo = new QComboBox(m_filterRow);
    // 140 px only in Wide; Medium/Narrow let it shrink (updateFilterBarMode).
    m_appCombo->setMinimumWidth(140);
    m_appCombo->setAccessibleName(tr("Source application filter"));
    m_appCombo->addItem(tr("All sources"), QString());
    refreshAppFilter();
    comboPolicy(m_appCombo);
    filterRow->addWidget(m_appCombo, 1);

    m_tagCombo = new QComboBox(m_filterRow);
    m_tagCombo->setToolTip(tr("Filter by tag — an entry matches when it carries the selected tag."));
    m_tagCombo->setAccessibleName(tr("Tag filter"));
    refreshTagFilter();
    comboPolicy(m_tagCombo);
    filterRow->addWidget(m_tagCombo);

    m_sortCombo = new QComboBox(m_filterRow);
    m_sortCombo->setAccessibleName(tr("Sort order"));
    m_sortCombo->addItem(tr("Newest first"), int(FilterSpec::SortMode::Newest));
    m_sortCombo->addItem(tr("Oldest first"), int(FilterSpec::SortMode::Oldest));
    m_sortCombo->addItem(tr("Most used"), int(FilterSpec::SortMode::MostUsed));
    m_sortCombo->setToolTip(tr("Order of the history list. Most used ranks entries by their use count."));
    {
        const int sortIndex = m_sortCombo->findData(m_ctx.settings()->sortMode());
        if (sortIndex >= 0)
            m_sortCombo->setCurrentIndex(sortIndex);
    }
    comboPolicy(m_sortCombo);
    filterRow->addWidget(m_sortCombo);

    filterStack->addWidget(m_filterRow);
    layout->addWidget(m_filterWidget);

    // Typed field filters (app:, type:, …) and rejected values, mirrored from
    // the search box so the effective query is visible while typing.
    m_queryHint = UiHelpers::makeHint(QString(), central, /*richText=*/false);
    m_queryHint->setVisible(false);
    layout->addWidget(m_queryHint);

    // R2: removable active-filter chips (one per active filter, × clears it).
    m_chipRow = new QWidget(central);
    m_chipLayout = new QHBoxLayout(m_chipRow);
    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setSpacing(DesignTokens::ChipSpacing);
    m_chipLayout->addStretch(1);
    m_chipRow->setVisible(false);
    layout->addWidget(m_chipRow);

    // Timeline strip: 14-day histogram, click to filter by day
    m_timeline = new TimelineStrip(m_ctx.storage(), central);
    m_timeline->setVisible(m_ctx.settings()->timelineEnabled());
    layout->addWidget(m_timeline);

    // --- list + preview -----------------------------------------------------
    auto *splitter = new QSplitter(Qt::Horizontal, central);

    m_model = new ClipboardListModel(m_ctx.storage(), this);
    m_delegate = new EntryDelegate(m_ctx.bookmarks(), m_ctx.settings(), this);
    m_delegate->setRowPadding(DesignTokens::rowPaddingForDensity(m_ctx.settings()->listDensity()));

    m_list = new HistoryListView(splitter);
    m_list->setModel(m_model);
    m_list->setItemDelegate(m_delegate);
    m_list->setAccessibleName(tr("Clipboard history"));
    m_list->setAccessibleDescription(tr("Entries copied recently; Enter pastes the selected one"));
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

    // "No entries yet" / "nothing matches this filter", over the empty viewport.
    m_emptyHint = nullptr;
    m_list->viewport()->installEventFilter(this);

    // U11 skeleton rows while the first page is loading.
    m_listSkeleton = new QWidget(m_list->viewport());
    m_listSkeleton->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *skelLayout = new QVBoxLayout(m_listSkeleton);
    skelLayout->setContentsMargins(DesignTokens::SpaceM, DesignTokens::SpaceS,
                                   DesignTokens::SpaceM, DesignTokens::SpaceS);
    skelLayout->setSpacing(DesignTokens::SpaceXs);
    for (int i = 0; i < 6; ++i) {
        auto *row = UiHelpers::makeSkeleton(m_listSkeleton);
        row->setFixedHeight(DesignTokens::IconL + DesignTokens::SpaceXs);
        skelLayout->addWidget(row);
    }
    skelLayout->addStretch(1);
    m_listSkeleton->hide();

    m_preview = new PreviewPane(splitter);
    if (m_ctx.scripts()) m_preview->setScriptManager(m_ctx.scripts());
    m_preview->setSettingsManager(m_ctx.settings());
    connect(m_preview, &PreviewPane::copyToClipboardRequested, this,
            [this](const QString &text) {
                if (text.isEmpty())
                    return;
                ClipboardRecord record;
                record.textData = text;
                record.preview = text.left(256);
                m_ctx.autoPaster()->copyToClipboard(record);
            });
    connect(m_preview, &PreviewPane::pinRequested, this, [this](qint64 id) {
        ClipboardRecord rec;
        if (id != 0 && m_ctx.storage()->fetchFull(id, &rec))
            m_ctx.storage()->setPinned(id, !rec.pinned);
    });
    connect(m_preview, &PreviewPane::closeRequested, this, [this] {
        if (m_previewDocked && m_previewDock)
            m_previewDock->hide();
    });
    m_preview->setMinimumWidth(0); // mode-aware: Wide keeps breathing room (applyResponsiveMode)
    splitter->addWidget(m_preview);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 260});
    m_splitter = splitter;
    layout->addWidget(splitter, 1);

    // R2: bulk-action bar for multi-select (hidden unless >1 row selected).
    m_bulkBar = new QWidget(central);
    auto *bulkLayout = new QHBoxLayout(m_bulkBar);
    bulkLayout->setContentsMargins(DesignTokens::SpaceS, DesignTokens::SpaceXs,
                                   DesignTokens::SpaceS, DesignTokens::SpaceXs);
    bulkLayout->setSpacing(DesignTokens::SpaceS);
    m_bulkCount = new QLabel(m_bulkBar);
    m_bulkCount->setAccessibleName(tr("Selected entries count"));
    bulkLayout->addWidget(m_bulkCount);
    const auto addBulk = [&](const QString &text, const QString &icon, auto handler) {
        auto *button = new QPushButton(QIcon::fromTheme(icon), text, m_bulkBar);
        button->setMinimumHeight(DesignTokens::TouchTargetCompact);
        connect(button, &QPushButton::clicked, this, handler);
        bulkLayout->addWidget(button);
    };
    addBulk(tr("Pin"), QStringLiteral("bookmarks"), [this] { bulkPin(true); });
    addBulk(tr("Unpin"), QStringLiteral("bookmarks"), [this] { bulkPin(false); });
    addBulk(tr("Tag…"), QStringLiteral("tag"), [this] { bulkTag(); });
    addBulk(tr("Group…"), QStringLiteral("folder"), [this] { bulkMoveToGroup(); });
    addBulk(tr("Export…"), QStringLiteral("document-save"), [this] { bulkExport(); });
    addBulk(tr("Delete"), QStringLiteral("edit-delete"), [this] { bulkDelete(); });
    auto *clearSelection = new QPushButton(tr("Clear"), m_bulkBar);
    clearSelection->setMinimumHeight(DesignTokens::TouchTargetCompact);
    connect(clearSelection, &QPushButton::clicked, this,
            [this] { m_list->selectionModel()->clearSelection(); });
    bulkLayout->addWidget(clearSelection);
    bulkLayout->addStretch(1);
    m_bulkBar->setVisible(false);
    layout->addWidget(m_bulkBar);

    // Medium/Narrow drawer: PreviewPane is reparented here, never recreated.
    m_previewDock = new QDockWidget(tr("Preview"), central);
    m_previewDock->setObjectName(QStringLiteral("previewDock"));
    m_previewDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                               | QDockWidget::DockWidgetFloatable);
    m_previewDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    m_previewDock->hide();

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

    // "More" overflow for Medium/Narrow (U3): secondary actions move here.
    m_moreButton = new QToolButton(m_toolbar);
    m_moreButton->setText(tr("More"));
    m_moreButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    m_moreButton->setAccessibleName(tr("More actions"));
    m_moreButton->setVisible(false);
    m_toolbar->addWidget(m_moreButton);
    m_overflowActions = {m_pinnedOnlyAction, m_sensitiveAction, m_deleteFilteredAction,
                         m_groupsAction, snipAction, chainAction};

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
    // Ctrl+K is registered exactly once, on the toolbar palette action; extra
    // QShortcut/keyPressEvent registrations made the sequence ambiguous.
    // Ctrl+1…9: paste the first nine entries of the current filter, mirroring
    // the number keys in the quick-paste popup.
    for (int i = 1; i <= 9; ++i) {
        auto *numberKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i)), this);
        connect(numberKey, &QShortcut::activated, this, [this, i] {
            const QModelIndex index = m_model->index(i - 1, 0);
            if (index.isValid())
                pasteEntry(index.data(ClipboardListModel::IdRole).toLongLong());
        });
    }
    connect(m_search, &QLineEdit::textChanged, this,
            [this] { m_searchDebounce->start(); });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        commitCurrentSearch();
        applyCurrentFilter();
    });

    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::applyCurrentFilter);
    if (m_timeline) {
        connect(m_timeline, &TimelineStrip::daySelected, this, [this](qint64 from, qint64 to){
            // The timeline picked the day itself: set the range without going
            // through the combo's "Custom range…" entry, which would open the
            // date dialog for a click that already answered it.
            const QSignalBlocker blocker(m_dateCombo);
            if (from == 0 && to == 0) {
                m_lastRange = DateRange{};
                m_dateCombo->setCurrentIndex(0);
            } else {
                m_lastRange = DateRange{true, from, to};
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
                if (m_timeline)
                    m_timeline->clearSelection(); // a typed range, not a clicked day
                if (!m_lastRange.isValid)
                    m_dateCombo->setCurrentIndex(0);
                else
                    applyCurrentFilter();
            } else {
                m_dateCombo->setCurrentIndex(0); // revert
            }
        } else {
            if (m_timeline)
                m_timeline->clearSelection();
            applyCurrentFilter();
        }
    });
    connect(m_appCombo, &QComboBox::currentIndexChanged, this, &MainWindow::applyCurrentFilter);
    connect(m_tagCombo, &QComboBox::currentIndexChanged, this, &MainWindow::applyCurrentFilter);
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            m_ctx.settings()->setSortMode(m_sortCombo->itemData(index).toInt());
        applyCurrentFilter();
    });

    auto selectionModel = m_list->selectionModel();
    connect(selectionModel, &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(m_list, &QListView::activated, this, &MainWindow::onActivated);
    connect(m_list, &QListView::customContextMenuRequested, this, &MainWindow::showContextMenu);

    connect(m_ctx.storage(), &StorageManager::entryAdded, this,
            [this] { refreshAppFilter(); });
    // Empty-list states: the model reports when the first page landed, the
    // plain model signals cover every later change.
    connect(m_model, &ClipboardListModel::initialPageLoaded, this,
            [this](bool) { hideListSkeleton(); updateEmptyState(); });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this] { updateEmptyState(); });
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this] { updateEmptyState(); });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] { updateEmptyState(); });
    connect(m_ctx.storage(), &StorageManager::storageReset, this, [this] {
        refreshAppFilter();
        refreshTagFilter();
        m_preview->showEmpty();
    });
    // Live appearance changes (theme is applied globally in ApplicationContext;
    // here we restyle the toolbar button mode and the timeline visibility).
    connect(m_ctx.settings(), &SettingsManager::changed, this, [this] {
        if (m_timeline)
            m_timeline->setVisible(m_ctx.settings()->timelineEnabled()
                                   && width() >= DesignTokens::TimelineCollapseWidth);
        if (m_delegate) {
            m_delegate->setRowPadding(
                DesignTokens::rowPaddingForDensity(m_ctx.settings()->listDensity()));
            m_list->viewport()->update();
        }
        if (!m_toolbar) return;
        m_toolbar->setToolButtonStyle(m_ctx.settings()->toolbarIconOnly()
                                          ? Qt::ToolButtonIconOnly
                                          : Qt::ToolButtonTextBesideIcon);
    });
    if (m_filtersButton) {
        auto *menu = new QMenu(m_filtersButton);
        connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            menu->clear();
            const auto addCombo = [this, menu](const QString &title, QComboBox *combo) {
                auto *widgetAction = new QWidgetAction(menu);
                auto *row = new QWidget(menu);
                auto *rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(DesignTokens::SpaceS, DesignTokens::SpaceXs,
                                              DesignTokens::SpaceS, DesignTokens::SpaceXs);
                auto *label = new QLabel(title, row);
                // A live mirror: selecting in the menu drives the real combo.
                auto *mirror = new QComboBox(row);
                for (int i = 0; i < combo->count(); ++i)
                    mirror->addItem(combo->itemText(i), combo->itemData(i));
                mirror->setCurrentIndex(combo->currentIndex());
                connect(mirror, &QComboBox::currentIndexChanged, this,
                        [combo](int index) { combo->setCurrentIndex(index); });
                rowLayout->addWidget(label);
                rowLayout->addWidget(mirror, 1);
                widgetAction->setDefaultWidget(row);
                menu->addAction(widgetAction);
            };
            addCombo(tr("Type:"), m_typeCombo);
            addCombo(tr("Date:"), m_dateCombo);
            addCombo(tr("Source:"), m_appCombo);
            addCombo(tr("Tag:"), m_tagCombo);
            addCombo(tr("Sort:"), m_sortCombo);
        });
        m_filtersButton->setMenu(menu);
    }
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

void MainWindow::refreshTagFilter()
{
    const QString current = m_tagCombo->currentData().toString();
    m_tagCombo->blockSignals(true);
    m_tagCombo->clear();
    m_tagCombo->addItem(tr("All tags"), QString());
    const QStringList tags = m_ctx.storage()->allTags();
    for (const QString &tag : tags)
        m_tagCombo->addItem(tag, tag);
    m_tagCombo->setEnabled(tags.size() > 0);
    const int index = m_tagCombo->findData(current);
    if (index >= 0)
        m_tagCombo->setCurrentIndex(index);
    m_tagCombo->blockSignals(false);
}

void MainWindow::applySavedSearch(const FilterSpec &filter)
{
    // Mirror the saved filter onto the widgets so the UI stays the source of
    // truth; anything not representable in the presets lands in "Custom range…".
    // has:ocr, /regex/ and -exclusions have no widget, so they go back into the
    // search box in query syntax — dropping them would change the search.
    QString queryText = filter.searchText;
    if (filter.hasOcrOnly)
        queryText += QStringLiteral(" has:ocr");
    if (!filter.regexText.isEmpty())
        queryText += QStringLiteral(" /%1/").arg(filter.regexText);
    if (!filter.excludeText.isEmpty())
        queryText += QLatin1Char(' ') + SearchEngine::negatedTerms(filter.excludeText);
    m_search->setText(queryText.trimmed());
    m_searchScope = int(filter.searchScope);
    if (m_scopeAction)
        m_scopeAction->setToolTip(tr("Search scope: %1").arg(scopeLabel(m_searchScope)));
    const int typeIndex = m_typeCombo->findData(filter.contentType);
    m_typeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
    const int appIndex = m_appCombo->findData(filter.sourceApp);
    m_appCombo->setCurrentIndex(appIndex >= 0 ? appIndex : 0);
    const QString tag = filter.tags.isEmpty() ? QString() : filter.tags.first();
    const int tagIndex = m_tagCombo->findData(tag);
    m_tagCombo->setCurrentIndex(tagIndex >= 0 ? tagIndex : 0);
    const int sortIndex = m_sortCombo->findData(int(filter.sortMode));
    m_sortCombo->setCurrentIndex(sortIndex >= 0 ? sortIndex : 0);
    m_groupFilter = filter.groupId.value_or(0);
    if (m_pinnedOnlyAction)
        m_pinnedOnlyAction->setChecked(filter.pinnedOnly);
    if (m_sensitiveAction)
        m_sensitiveAction->setChecked(filter.sensitiveOnly);
    if (filter.fromMs > 0 || filter.toMs > 0) {
        m_lastRange = DateRange{true, filter.fromMs, filter.toMs};
        const int customIndex = m_dateCombo->findData(99);
        if (customIndex >= 0) {
            // Mirrored, not chosen: without the blocker the combo's handler
            // would pop the "Custom range…" dialog at the user.
            const QSignalBlocker blocker(m_dateCombo);
            m_dateCombo->setCurrentIndex(customIndex);
        }
    } else {
        m_lastRange = DateRange{};
        const QSignalBlocker blocker(m_dateCombo);
        m_dateCombo->setCurrentIndex(0);
    }
    applyCurrentFilter();
}

void MainWindow::buildSavedSearchesMenu()
{
    QMenu *menu = m_savedSearchesButton->menu();
    if (!menu)
        return;
    menu->clear();

    QAction *saveCurrent = menu->addAction(QIcon::fromTheme(QStringLiteral("document-save")),
                                           tr("Save current filter…"));
    connect(saveCurrent, &QAction::triggered, this, [this] {
        const FilterSpec current = m_model->filter();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save search"),
                                                   tr("Name for this search:"),
                                                   QLineEdit::Normal,
                                                   current.searchText.isEmpty()
                                                       ? QString()
                                                       : current.searchText,
                                                   &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        if (m_ctx.storage()->addSavedSearch(name, current) == 0)
            QMessageBox::warning(this, tr("Save search"), tr("Could not save the search."));
    });
    menu->addSeparator();

    const auto searches = m_ctx.storage()->savedSearches();
    if (searches.isEmpty()) {
        QAction *empty = menu->addAction(tr("(no saved searches yet)"));
        empty->setEnabled(false);
        return;
    }
    for (const SavedSearch &search : searches) {
        QAction *apply = menu->addAction(QIcon::fromTheme(QStringLiteral("folder-saved-search")),
                                         search.name);
        connect(apply, &QAction::triggered, this,
                [this, filter = search.filter] { applySavedSearch(filter); });
    }
    menu->addSeparator();
    QAction *remove = menu->addAction(QIcon::fromTheme(QStringLiteral("list-remove")),
                                      tr("Delete saved search…"));
    remove->setEnabled(true);
    connect(remove, &QAction::triggered, this, [this] {
        const auto searches = m_ctx.storage()->savedSearches();
        if (searches.isEmpty())
            return;
        QStringList names;
        for (const SavedSearch &search : searches)
            names << search.name;
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Delete saved search"),
                                                     tr("Search to delete:"), names, 0, false,
                                                     &ok);
        if (!ok)
            return;
        for (const SavedSearch &search : searches) {
            if (search.name == chosen) {
                m_ctx.storage()->removeSavedSearch(search.id);
                return;
            }
        }
    });
}

void MainWindow::applyCurrentFilter()
{
    FilterSpec base;
    base.contentType = m_typeCombo->currentData().toInt();
    if (m_groupFilter != 0)
        base.groupId = m_groupFilter;
    if (m_sensitiveAction && m_sensitiveAction->isChecked())
        base.sensitiveOnly = true;
    if (m_pinnedOnlyAction && m_pinnedOnlyAction->isChecked())
        base.pinnedOnly = true;
    const QString tagFilter = m_tagCombo ? m_tagCombo->currentData().toString() : QString();
    if (!tagFilter.isEmpty())
        base.tags << tagFilter;
    if (m_sortCombo)
        base.sortMode = static_cast<FilterSpec::SortMode>(m_sortCombo->currentData().toInt());

    const int datePreset = m_dateCombo->currentData().toInt();
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime startOfToday(now.date(), QTime(0, 0));
    switch (datePreset) {
    case 1:
        base.fromMs = startOfToday.toMSecsSinceEpoch();
        break;
    case 2: {
        const QDateTime startOfYesterday(startOfToday.date().addDays(-1), QTime(0, 0));
        base.fromMs = startOfYesterday.toMSecsSinceEpoch();
        base.toMs = startOfToday.toMSecsSinceEpoch() - 1;
        break;
    }
    case 3:
        base.fromMs = now.addDays(-7).toMSecsSinceEpoch();
        break;
    case 4:
        base.fromMs = now.addMonths(-1).toMSecsSinceEpoch();
        break;
    case 99:
        if (m_lastRange.isValid) {
            base.fromMs = m_lastRange.fromMs;
            base.toMs = m_lastRange.toMs;
        }
        break;
    default:
        break;
    }
    base.sourceApp = m_appCombo->currentData().toString();
    base.searchScope = static_cast<FilterSpec::SearchScope>(m_searchScope);

    // The search box carries free text plus field filters (app:, type:, …);
    // typed fields override the matching toolbar presets.
    const SearchEngine::ParsedQuery parsed = SearchEngine::parseQuery(m_search->text(), base);
    const FilterSpec filter = parsed.filter;
    showListSkeleton();
    m_model->setFilter(filter);
    if (m_timeline) m_timeline->setFilter(filter);

    // Mark the searched words in the list rows and the text preview. Operators
    // (AND/OR/NOT) and quotes are dropped; two-letter terms are too noisy.
    QStringList terms;
    for (const QString &word : SearchEngine::textTerms(parsed.text)) {
        if (word.size() >= 2 && !terms.contains(word, Qt::CaseInsensitive))
            terms << word;
    }
    m_delegate->setSearchTerms(terms);
    m_preview->setSearchTerms(terms);
    m_list->viewport()->update();

    if (m_queryHint) {
        QString hint;
        if (!parsed.applied.isEmpty())
            hint = tr("Filtering by %1").arg(parsed.applied.join(QStringLiteral(" · ")));
        if (!parsed.problems.isEmpty()) {
            if (!hint.isEmpty())
                hint += QStringLiteral("  •  ");
            hint += parsed.problems.join(QStringLiteral(" · "));
        }
        m_queryHint->setText(hint);
        m_queryHint->setVisible(!hint.isEmpty());
    }
    rebuildFilterChips();
    updateFilterBarMode();
    updateActionStates();
}

// The list says "no entries yet" and "nothing matches this filter" are two
// different situations; which one shows comes from the active filter.
void MainWindow::updateEmptyState()
{
    if (!m_model)
        return;
    const bool empty = m_model->rowCount() == 0;
    if (m_emptyHint) {
        m_emptyHint->deleteLater();
        m_emptyHint = nullptr;
    }
    if (!empty)
        return;
    const bool trivial = m_model->filter().isTrivial();
    m_emptyHint = UiHelpers::makeEmptyState(
        trivial ? QStringLiteral("edit-paste") : QStringLiteral("view-filter"),
        trivial ? tr("No entries yet") : tr("Nothing matches this filter"),
        trivial ? tr("Copy something and it will show up here. Shortcuts: Meta+V quick paste, "
                     "Ctrl+K commands.")
                : tr("Try clearing some filters."),
        m_list->viewport(),
        trivial ? QString() : tr("Clear filters"),
        trivial ? std::function<void()>() : [this] { clearAllFilters(); }
    );
    m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_emptyHint->setGeometry(m_list->viewport()->rect());
    m_emptyHint->show();
}

void MainWindow::showListSkeleton()
{
    if (!m_listSkeleton || !m_list)
        return;
    m_listSkeleton->setGeometry(m_list->viewport()->rect());
    m_listSkeleton->raise();
    m_listSkeleton->show();
}

void MainWindow::hideListSkeleton()
{
    if (m_listSkeleton)
        m_listSkeleton->hide();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_emptyHint && watched == m_list->viewport() && event->type() == QEvent::Resize) {
        m_emptyHint->setGeometry(m_list->viewport()->rect());
        if (m_listSkeleton)
            m_listSkeleton->setGeometry(m_list->viewport()->rect());
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onSelectionChanged()
{
    const QModelIndexList selected = m_list->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        m_selectedId = 0;
        m_preview->showEmpty();
        // Medium: no selection means the drawer has nothing to show.
        if (m_previewDocked && m_previewDock
            && m_shellMode == DesignTokens::ShellMode::Medium)
            m_previewDock->hide();
    } else {
        m_selectedId = selected.first().data(ClipboardListModel::IdRole).toLongLong();
        ClipboardRecord full;
        if (m_ctx.storage()->fetchFull(m_selectedId, &full))
            m_preview->showRecord(full);
        // Medium: selecting reveals the bottom drawer (Wide/Narrow unaffected).
        if (m_previewDocked && m_previewDock
            && m_shellMode == DesignTokens::ShellMode::Medium) {
            m_previewDock->show();
            UiHelpers::animate(m_previewDock, UiHelpers::MotionKind::SlideSide);
        }
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
    // A paste while a search is active means the query was useful: remember it.
    commitCurrentSearch();
    m_ctx.pasteEntry(entryId);
}

void MainWindow::pasteCurrent()
{
    if (m_selectedId != 0)
        pasteEntry(m_selectedId);
}

void MainWindow::commitCurrentSearch()
{
    const QString text = m_search->text().trimmed();
    if (!text.isEmpty())
        m_ctx.settings()->addRecentSearch(text);
}

void MainWindow::showScopeMenu()
{
    QMenu menu(this);
    const int scopes[] = {int(FilterSpec::SearchScope::All), int(FilterSpec::SearchScope::Preview),
                          int(FilterSpec::SearchScope::FullText), int(FilterSpec::SearchScope::Ocr)};
    for (const int scope : scopes) {
        QAction *action = menu.addAction(scopeLabel(scope));
        action->setCheckable(true);
        action->setChecked(scope == m_searchScope);
        connect(action, &QAction::triggered, this, [this, scope] {
            m_searchScope = scope;
            m_ctx.settings()->setSearchScope(scope);
            m_scopeAction->setToolTip(tr("Search scope: %1").arg(scopeLabel(scope)));
            applyCurrentFilter();
        });
    }
    menu.exec(m_search->mapToGlobal(QPoint(0, m_search->height())));
}

void MainWindow::showRecentSearches()
{
    QMenu menu(this);
    const QStringList recents = m_ctx.settings()->recentSearches();
    if (recents.isEmpty()) {
        QAction *empty = menu.addAction(tr("No recent searches yet"));
        empty->setEnabled(false);
    } else {
        for (const QString &query : recents) {
            QAction *action = menu.addAction(query);
            connect(action, &QAction::triggered, this, [this, query] {
                m_search->setText(query);
                commitCurrentSearch(); // selecting moves it to the front
                applyCurrentFilter();
            });
        }
        menu.addSeparator();
        QAction *clear = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")),
                                        tr("Clear recent searches"));
        connect(clear, &QAction::triggered, this,
                [this] { m_ctx.settings()->clearRecentSearches(); });
    }
    menu.exec(m_search->mapToGlobal(QPoint(0, m_search->height())));
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
    QVector<ClipboardRecord> deleted;
    deleted.reserve(ids.size());
    for (const qint64 id : ids) {
        ClipboardRecord full;
        if (m_ctx.storage()->fetchFull(id, &full))
            deleted.append(full);
    }
    m_ctx.storage()->removeEntries(ids);
    showUndoToast(tr("%n entry(ies) deleted", nullptr, ids.size()), deleted);
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
    QVector<ClipboardRecord> deleted;
    deleted.reserve(all.size());
    for (const ClipboardRecord &record : all) {
        ClipboardRecord full;
        if (m_ctx.storage()->fetchFull(record.id, &full))
            deleted.append(full);
    }
    m_ctx.storage()->removeEntries(ids);
    showUndoToast(tr("%n entry(ies) deleted", nullptr, ids.size()), deleted);
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

    // "Paste as" variants reshape the payload; the stored entry is untouched.
    {
        const qint64 entryId = index.data(ClipboardListModel::IdRole).toLongLong();
        const int type = index.data(ClipboardListModel::TypeRole).toInt();
        const bool isText = type == int(ContentType::Text) || type == int(ContentType::RichText);
        const bool isImage = type == int(ContentType::Image);
        if (isText || isImage) {
            QMenu *pasteAs = menu.addMenu(tr("Paste as"));
            const auto addVariant = [this, pasteAs, entryId](
                                        const QString &label,
                                        ApplicationContext::PasteVariant variant, bool enabled) {
                QAction *a = pasteAs->addAction(label);
                a->setEnabled(enabled);
                connect(a, &QAction::triggered, this,
                        [this, entryId, variant] { m_ctx.pasteEntry(entryId, variant); });
            };
            addVariant(tr("Plain text"), ApplicationContext::PasteVariant::PlainText, isText);
            addVariant(tr("UPPERCASE"), ApplicationContext::PasteVariant::UpperCase, isText);
            addVariant(tr("lowercase"), ApplicationContext::PasteVariant::LowerCase, isText);
            addVariant(tr("With timestamp"), ApplicationContext::PasteVariant::WithTimestamp,
                       isText);
            addVariant(tr("Image → PNG file"), ApplicationContext::PasteVariant::ImageAsPngFile,
                       isImage);
        }
    }

    // Tags submenu: check the tags the entry carries; toggling adds/removes.
    {
        const qint64 entryId = index.data(ClipboardListModel::IdRole).toLongLong();
        const QStringList entryTags = m_ctx.storage()->tagsForEntry(entryId);
        const QStringList availableTags = m_ctx.storage()->allTags();
        QMenu *tagsMenu = menu.addMenu(tr("Tags"));
        for (const QString &tag : availableTags) {
            QAction *a = tagsMenu->addAction(tag);
            a->setCheckable(true);
            a->setChecked(entryTags.contains(tag));
            connect(a, &QAction::toggled, this, [this, entryId, tag](bool checked) {
                if (checked)
                    m_ctx.storage()->addTag(entryId, tag);
                else
                    m_ctx.storage()->removeTag(entryId, tag);
                refreshTagFilter();
                // Re-run the filter only when the list is tag-filtered, so
                // casual tagging never jumps the scroll position.
                if (!m_tagCombo->currentData().toString().isEmpty())
                    applyCurrentFilter();
            });
        }
        tagsMenu->addSeparator();
        QAction *newTag = tagsMenu->addAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                              tr("New tag…"));
        connect(newTag, &QAction::triggered, this, [this, entryId] {
            bool ok = false;
            const QString tag = QInputDialog::getText(this, tr("New tag"), tr("Tag name:"),
                                                      QLineEdit::Normal, {}, &ok);
            if (!ok || tag.trimmed().isEmpty())
                return;
            m_ctx.storage()->addTag(entryId, tag);
            refreshTagFilter();
            applyCurrentFilter();
        });
    }

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
    // Snapshot for Undo before clearing (bounded: full payloads, may be large
    // but clear-history is explicit and rare).
    const QVector<ClipboardRecord> deleted =
        m_ctx.storage()->fetchAllFull(FilterSpec{});
    const int removed = m_ctx.storage()->clearHistory(includePinned->isChecked());
    QVector<ClipboardRecord> restorable;
    for (const ClipboardRecord &record : deleted) {
        if (!includePinned->isChecked() && record.pinned)
            continue;
        restorable.append(record);
    }
    showUndoToast(tr("%n entry(ies) deleted", nullptr, removed), restorable);
}

void MainWindow::updateActionStates()
{
    const bool hasSelection = m_selectedId != 0;
    m_pasteAction->setEnabled(hasSelection);
    m_copyAction->setEnabled(hasSelection);
    m_deleteAction->setEnabled(hasSelection);
    updateBulkBar();
}

void MainWindow::updateBulkBar()
{
    if (!m_bulkBar || !m_list)
        return;
    const int count = m_list->selectionModel()
        ? m_list->selectionModel()->selectedIndexes().size()
        : 0;
    m_bulkBar->setVisible(count > 1);
    if (m_bulkCount)
        m_bulkCount->setText(tr("%n selected", nullptr, count));
}

QList<qint64> bulkSelectedIds(QListView *list)
{
    QList<qint64> ids;
    if (!list || !list->selectionModel())
        return ids;
    for (const QModelIndex &index : list->selectionModel()->selectedIndexes())
        ids.append(index.data(ClipboardListModel::IdRole).toLongLong());
    return ids;
}

void MainWindow::bulkPin(bool pinned)
{
    const QList<qint64> ids = bulkSelectedIds(m_list);
    if (ids.isEmpty())
        return;
    m_ctx.storage()->beginBulk();
    for (const qint64 id : ids)
        m_ctx.storage()->setPinned(id, pinned);
    m_ctx.storage()->endBulk();
}

void MainWindow::bulkTag()
{
    const QList<qint64> ids = bulkSelectedIds(m_list);
    if (ids.isEmpty())
        return;
    bool ok = false;
    const QString tag = QInputDialog::getText(this, tr("Tag selection"), tr("Tag name:"),
                                              QLineEdit::Normal, QString(), &ok);
    const QString name = tag.trimmed();
    if (!ok || name.isEmpty())
        return;
    m_ctx.storage()->beginBulk();
    for (const qint64 id : ids)
        m_ctx.storage()->addTag(id, name);
    m_ctx.storage()->endBulk();
    refreshTagFilter();
}

void MainWindow::bulkMoveToGroup()
{
    const QList<qint64> ids = bulkSelectedIds(m_list);
    if (ids.isEmpty())
        return;
    QStringList names;
    QHash<QString, qint64> byName;
    for (const BookmarkGroup &group : m_ctx.bookmarks()->groups()) {
        names.append(group.name);
        byName.insert(group.name, group.id);
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Move to group"), tr("Group:"),
                                                 names, 0, true, &ok);
    const QString name = chosen.trimmed();
    if (!ok || name.isEmpty())
        return;
    qint64 groupId = byName.value(name, 0);
    if (groupId == 0) {
        groupId = m_ctx.bookmarks()->createGroup(name);
        if (groupId == 0)
            return;
    }
    for (const qint64 id : ids)
        m_ctx.bookmarks()->assignEntry(id, groupId);
    m_delegate->clearGroupCache();
}

void MainWindow::bulkExport()
{
    const QList<qint64> ids = bulkSelectedIds(m_list);
    if (ids.isEmpty())
        return;
    // Image-only bulk export (U17): the dialog opens on the selection, other
    // scopes (filter/all/pinned/group) are one click away inside it.
    runImageExport(ExportImportManager::ImageExportRequest::Scope::Selection, ids);
}

void MainWindow::runImageExport(ExportImportManager::ImageExportRequest::Scope initialScope,
                                const QList<qint64> &selectedIds)
{
    using ImageScope = ExportImportManager::ImageExportRequest::Scope;
    const FilterSpec currentFilter = m_model ? m_model->filter() : FilterSpec{};
    ExportImportDialogs::ImageExportDialog dialog(m_ctx.bookmarks(), selectedIds, currentFilter,
                                                  !currentFilter.isTrivial(), this);
    dialog.setScope(initialScope);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ExportImportManager::ImageExportRequest request;
    request.scope = dialog.scope();
    request.entryIds = selectedIds;
    request.filter = currentFilter;
    request.groupId = dialog.groupId();
    request.dir = dialog.folder();
    request.fileFormat = dialog.fileFormat();
    request.jpegQuality = dialog.jpegQuality();
    request.includeSensitive = dialog.includeSensitive();
    request.includeText = dialog.includeText();
    if (request.dir.isEmpty()) {
        QMessageBox::warning(this, tr("Export images"), tr("Choose a target folder first."));
        return;
    }
    // JPEG conversion needs QImage, which core must not depend on: the dialog
    // supplies the format, this layer supplies the encoder behind core's hook.
    ExportImportManager::ImageEncoder encoder;
    if (request.fileFormat == ExportImportManager::ImageExportRequest::ImageFileFormat::Jpeg) {
        const int quality = request.jpegQuality;
        encoder = [quality](const QByteArray &storedPng, qint64 entryId, QString *extension,
                            QString *error) {
            return ExportImportDialogs::encodeImageForExport(storedPng, entryId, quality,
                                                             extension, error);
        };
    }

    // Chunked synchronous run on the GUI thread (same thread as the storage
    // connection): the progress callback pumps the event loop between batches
    // so Cancel takes effect promptly without any worker-thread SQL.
    std::atomic<bool> cancel{false};
    QProgressDialog progress(tr("Exporting images…"), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setLabelText(tr("Exporting images…"));
    connect(&progress, &QProgressDialog::canceled, this, [&cancel] {
        cancel.store(true, std::memory_order_relaxed);
    });
    progress.show();
    const auto result = m_ctx.io()->exportImages(
        request, &cancel,
        [&](int exported, int skipped, qint64 bytes) {
            progress.setLabelText(
                tr("Exporting images… %1 written, %2 skipped (%3)")
                    .arg(exported)
                    .arg(skipped)
                    .arg(UiHelpers::humanSize(bytes)));
            QApplication::processEvents();
        },
        encoder);
    progress.close();

    if (result.canceled) {
        QMessageBox::information(this, tr("Export images"), result.error);
        return;
    }
    if (!result.ok) {
        QMessageBox::warning(this, tr("Export images"), result.error);
        return;
    }
    QString message = result.exported == 0
        ? tr("No image entries found in this scope.")
        : tr("%n image(s) written as %2 to %1.", nullptr, result.exported)
              .arg(request.dir)
              .arg(ExportImportManager::ImageExportRequest::imageFileFormatName(
                       request.fileFormat)
                       .toUpper());
    const int skipped = result.skippedNoBlob + result.skippedNonImage + result.skippedSensitive;
    if (skipped > 0) {
        message += QLatin1Char('\n')
            + tr("Skipped: %1 without a stored image, %2 of another type, %3 sensitive.")
                  .arg(result.skippedNoBlob)
                  .arg(result.skippedNonImage)
                  .arg(result.skippedSensitive);
        if (result.skippedSensitive > 0 && !request.includeSensitive)
            message += QLatin1Char(' ')
                + tr("Tick “Include entries flagged sensitive” to export those too.");
    }
    if (!result.manifestPath.isEmpty())
        message += QLatin1Char('\n') + tr("Manifest: %1.").arg(result.manifestPath);
    QMessageBox::information(this, tr("Export images"), message);
}

void MainWindow::bulkDelete()
{
    const QList<qint64> ids = bulkSelectedIds(m_list);
    if (ids.isEmpty())
        return;
    QMessageBox box(this);
    box.setWindowTitle(tr("Delete selection"));
    box.setText(tr("Delete the %n selected entry/entries?", nullptr, ids.size()));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;
    // Snapshot payloads for Undo before deleting.
    QVector<ClipboardRecord> deleted;
    deleted.reserve(ids.size());
    for (const qint64 id : ids) {
        ClipboardRecord full;
        if (m_ctx.storage()->fetchFull(id, &full))
            deleted.append(full);
    }
    m_ctx.storage()->removeEntries(ids);
    showUndoToast(tr("%n entry(ies) deleted", nullptr, ids.size()), deleted);
}

void MainWindow::showUndoToast(const QString &message, const QVector<ClipboardRecord> &deleted)
{
    QWidget *toast = UiHelpers::makeToast(
        message, this, tr("Undo"), [this, deleted] {
            m_ctx.storage()->beginBulk();
            for (const ClipboardRecord &record : deleted) {
                ClipboardRecord copy = record;
                copy.id = 0; // re-insert as new rows (ids are not reused)
                m_ctx.storage()->insertOrUpdate(copy);
            }
            m_ctx.storage()->endBulk();
        });
    toast->setAttribute(Qt::WA_DeleteOnClose, false); // makeToast owns lifetime
    const QPoint at(width() / 2 - toast->sizeHint().width() / 2,
                    height() - toast->sizeHint().height() - DesignTokens::ToastMargin * 3);
    toast->move(mapToGlobal(at));
    UiHelpers::animate(toast, UiHelpers::MotionKind::SlideUp);
    toast->show();
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
        m_palette->setSnippetManager(m_ctx.snippets());
        m_palette->setScriptManager(m_ctx.scripts());
        connect(m_palette, &CommandPalette::pasteRequested, this, &MainWindow::pasteEntry);
        connect(m_palette, &CommandPalette::copyRequested, this, [this](qint64 id){
            if (id == 0) id = m_selectedId; // ">copy": the main window's selection
            ClipboardRecord rec;
            if (id != 0 && m_ctx.storage()->fetchFull(id, &rec))
                m_ctx.autoPaster()->copyToClipboard(rec);
        });
        connect(m_palette, &CommandPalette::pinRequested, this, [this](qint64 id){
            if (id == 0) id = m_selectedId; // ">pin": the main window's selection
            if (id == 0) return;
            ClipboardRecord rec;
            if (!m_ctx.storage()->fetchFull(id, &rec)) return;
            if (m_ctx.storage()->setPinned(id, !rec.pinned)) {
                QSignalBlocker blocker(m_pinAction);
                m_pinAction->setChecked(!rec.pinned);
            }
        });
        // Whole-command palette entries act on the main window's selection.
        connect(m_palette, &CommandPalette::deleteRequested, this, &MainWindow::deleteSelected);
        connect(m_palette, &CommandPalette::tagRequested, this, [this](const QString &tag){
            const QString name = tag.trimmed();
            if (name.isEmpty() || m_selectedId == 0)
                return;
            if (m_ctx.storage()->addTag(m_selectedId, name)) {
                refreshTagFilter();
                if (!m_tagCombo->currentData().toString().isEmpty())
                    applyCurrentFilter(); // only when the list is tag-filtered
            }
        });
        connect(m_palette, &CommandPalette::groupRequested, this, [this](const QString &group){
            const QString name = group.trimmed();
            if (name.isEmpty() || m_selectedId == 0)
                return;
            qint64 groupId = 0;
            for (const BookmarkGroup &existing : m_ctx.bookmarks()->groups()) {
                if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
                    groupId = existing.id;
                    break;
                }
            }
            if (groupId == 0)
                groupId = m_ctx.bookmarks()->createGroup(name);
            if (groupId != 0 && m_ctx.bookmarks()->assignEntry(m_selectedId, groupId))
                m_delegate->clearGroupCache(); // signals may not fire for a group we just created
        });
        connect(m_palette, &CommandPalette::exportRequested, this,
                &MainWindow::exportHistoryToFormat);
        connect(m_palette, &CommandPalette::togglePauseRequested, this, [this]{
            m_ctx.setCapturePaused(!m_ctx.isCapturePaused());
        });
        connect(m_palette, &CommandPalette::settingsRequested, this, &MainWindow::openSettings);
        connect(m_palette, &CommandPalette::clearHistoryRequested, this, &MainWindow::clearHistory);
        connect(m_palette, &CommandPalette::commandExecuted, this, [this](const QString &id){
            m_ctx.settings()->addRecentPaletteCommand(id);
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
    // Argument completion works on the current tags/groups, so refresh them
    // right before the palette is shown.
    m_palette->setTagCandidates(m_ctx.storage()->allTags());
    QStringList groupNames;
    for (const BookmarkGroup &group : m_ctx.bookmarks()->groups())
        groupNames.append(group.name);
    groupNames.sort(Qt::CaseInsensitive);
    m_palette->setGroupCandidates(groupNames);
    m_palette->setRecentCommands(m_ctx.settings()->recentPaletteCommands());
    m_palette->setRecentSearches(m_ctx.settings()->recentSearches());
    m_palette->openPalette();
}

// ">export [format]": with a format the export runs straight away, without one
// the regular export dialog opens (preselected when a format was given).
// "images" opens the image-only folder flow instead of a file export.
void MainWindow::exportHistoryToFormat(const QString &format)
{
    if (format.trimmed().compare(QStringLiteral("images"), Qt::CaseInsensitive) == 0) {
        runImageExport(ExportImportManager::ImageExportRequest::Scope::CurrentFilter,
                       bulkSelectedIds(m_list));
        return;
    }
    ExportImportDialogs::ExportDialog dialog(m_ctx.bookmarks(), this);
    if (!format.trimmed().isEmpty()) {
        const QString wanted = format.trimmed().toLower();
        const QVector<ExportImportManager::ExportFormat> formats = {
            ExportImportManager::ExportFormat::Json, ExportImportManager::ExportFormat::Markdown,
            ExportImportManager::ExportFormat::Csv, ExportImportManager::ExportFormat::Html};
        for (ExportImportManager::ExportFormat candidate : formats) {
            if (ExportImportManager::formatId(candidate) == wanted) {
                dialog.setFormat(candidate);
                break;
            }
        }
    }
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
    QProgressDialog progressDialog(tr("Exporting…"), QString(), 0, 0, this);
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(0);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();
    QApplication::processEvents();
    const bool exported = m_ctx.io()->exportToFile(request, &error);
    progressDialog.close();
    if (!exported)
        QMessageBox::warning(this, tr("Export failed"), error);
    else
        QMessageBox::information(this, tr("Export finished"),
                                 tr("History exported to %1.").arg(request.path));
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (event && event->size().width() != event->oldSize().width())
        applyResponsiveMode(event->size().width());
}

void MainWindow::applyResponsiveMode(int width)
{
    const DesignTokens::ShellMode mode = DesignTokens::shellModeForWidth(width);
    const bool modeChanged = mode != m_shellMode;
    if (modeChanged)
        saveSplitterForMode();
    m_shellMode = mode;

    // Preview: Wide = side-by-side splitter, Medium = bottom drawer,
    // Narrow = floating drawer (groups overlay stays as-is).
    if (m_preview && m_splitter && m_previewDock) {
        if (mode == DesignTokens::ShellMode::Wide) {
            if (m_previewDocked) {
                m_previewDock->hide();
                m_previewDock->setWidget(nullptr);
                m_splitter->addWidget(m_preview);
                m_preview->setMinimumWidth(260);
                m_preview->show();
                m_previewDocked = false;
            } else {
                m_preview->setMinimumWidth(260);
            }
            if (modeChanged)
                restoreSplitterForMode();
            else if (m_splitter->sizes().isEmpty() || m_splitter->sizes().at(0) <= 0)
                m_splitter->setSizes({420, 260});
        } else {
            if (!m_previewDocked) {
                m_preview->setMinimumWidth(0);
                m_previewDock->setWidget(m_preview);
                m_preview->show();
                m_previewDocked = true;
            }
            const bool wantDockVisible =
                mode == DesignTokens::ShellMode::Medium && m_selectedId != 0;
            removeDockWidget(m_previewDock);
            addDockWidget(mode == DesignTokens::ShellMode::Medium ? Qt::BottomDockWidgetArea
                                                                  : Qt::RightDockWidgetArea,
                          m_previewDock);
            m_previewDock->setFloating(mode == DesignTokens::ShellMode::Narrow);
            m_previewDock->setVisible(wantDockVisible);
            if (wantDockVisible)
                m_previewDock->resize(
                    width, qMax(200, int(height() * DesignTokens::DrawerWidthFraction)));
        }
    }

    updateFilterBarMode();
    updateToolbarOverflow();
    if (m_timeline)
        m_timeline->setVisible(m_ctx.settings()->timelineEnabled()
                               && width >= DesignTokens::TimelineCollapseWidth);
}

void MainWindow::saveSplitterForMode()
{
    if (!m_ctx.settings()->rememberWindowGeometry() || !m_splitter || m_previewDocked)
        return;
    m_ctx.settings()->setSplitterStateForMode(shellModeIndex(), m_splitter->saveState());
}

void MainWindow::restoreSplitterForMode()
{
    if (!m_splitter || m_previewDocked)
        return;
    const QByteArray state = m_ctx.settings()->splitterStateForMode(shellModeIndex());
    if (!state.isEmpty())
        m_splitter->restoreState(state);
    else if (m_splitter->sizes().isEmpty() || m_splitter->sizes().at(0) <= 0)
        m_splitter->setSizes({420, 260});
}

void MainWindow::updateFilterBarMode()
{
    const bool collapsed = m_shellMode != DesignTokens::ShellMode::Wide;
    if (m_filterRow)
        m_filterRow->setVisible(!collapsed);
    if (m_filtersButton) {
        m_filtersButton->setVisible(collapsed);
        const int count = activeFilterCount();
        m_filtersButton->setText(count > 0 ? tr("Filters (%1)").arg(count) : tr("Filters"));
    }
    // App combo keeps its 140 px floor only in Wide; off Wide it may shrink.
    if (m_appCombo)
        m_appCombo->setMinimumWidth(m_shellMode == DesignTokens::ShellMode::Wide ? 140 : 0);
}

void MainWindow::updateToolbarOverflow()
{
    if (!m_toolbar || !m_moreButton)
        return;
    const bool overflow = m_shellMode != DesignTokens::ShellMode::Wide;
    m_moreButton->setVisible(overflow);
    if (overflow) {
        QMenu *menu = m_moreButton->menu();
        if (!menu) {
            menu = new QMenu(m_moreButton);
            m_moreButton->setMenu(menu);
        } else {
            menu->clear();
        }
        for (QAction *action : m_overflowActions) {
            if (!action)
                continue;
            m_toolbar->removeAction(action);
            menu->addAction(action);
        }
    } else {
        // Back to Wide: re-add after the Delete action, before the separator.
        if (QMenu *menu = m_moreButton->menu())
            menu->clear();
        for (QAction *action : m_overflowActions) {
            if (!action || m_toolbar->actions().contains(action))
                continue;
            // Insert before the first overflow-adjacent fixed action: Palette.
            QAction *before = nullptr;
            for (QAction *candidate : m_toolbar->actions()) {
                if (candidate->text() == tr("Palette")) {
                    before = candidate;
                    break;
                }
            }
            m_toolbar->insertAction(before, action);
        }
    }
}

QVector<MainWindow::FilterChip> MainWindow::currentFilterChips()
{
    // R2: one removable chip per active filter; × calls clear.
    QVector<FilterChip> chips;
    if (m_typeCombo && m_typeCombo->currentData().toInt() >= 0)
        chips.append({tr("Type: %1").arg(m_typeCombo->currentText()), tr("Type filter"),
                      [this] { clearTypeFilter(); }});
    if (m_dateCombo && m_dateCombo->currentData().toInt() != 0)
        chips.append({tr("Date: %1").arg(m_dateCombo->currentText()), tr("Date filter"),
                      [this] { clearDateFilter(); }});
    if (m_appCombo && !m_appCombo->currentData().toString().isEmpty())
        chips.append({tr("App: %1").arg(m_appCombo->currentText()), tr("Source app filter"),
                      [this] { clearAppFilter(); }});
    if (m_tagCombo && !m_tagCombo->currentData().toString().isEmpty())
        chips.append({tr("Tag: %1").arg(m_tagCombo->currentText()), tr("Tag filter"),
                      [this] { clearTagFilter(); }});
    if (m_groupFilter != 0) {
        QString name = tr("Group #%1").arg(m_groupFilter);
        for (const BookmarkGroup &group : m_ctx.bookmarks()->groups()) {
            if (group.id == m_groupFilter) {
                name = group.name;
                break;
            }
        }
        chips.append({tr("Group: %1").arg(name), tr("Group filter"),
                      [this] { clearGroupFilter(); }});
    }
    if (m_pinnedOnlyAction && m_pinnedOnlyAction->isChecked())
        chips.append({tr("Pinned only"), tr("Pinned filter"), [this] { clearPinnedFilter(); }});
    if (m_sensitiveAction && m_sensitiveAction->isChecked())
        chips.append({tr("Audit"), tr("Sensitive filter"), [this] { clearSensitiveFilter(); }});
    if (m_search && !m_search->text().trimmed().isEmpty())
        chips.append({tr("Search: %1").arg(m_search->text().trimmed().left(24)),
                      tr("Search filter"), [this] { clearSearchFilter(); }});
    return chips;
}

void MainWindow::rebuildFilterChips()
{
    if (!m_chipRow || !m_chipLayout)
        return;
    // Drop old chips (keep the trailing stretch).
    QList<QWidget *> old;
    for (int i = 0; i < m_chipLayout->count(); ++i) {
        if (QWidget *widget = m_chipLayout->itemAt(i)->widget())
            old.append(widget);
    }
    for (QWidget *widget : old) {
        m_chipLayout->removeWidget(widget);
        widget->deleteLater();
    }
    const QVector<FilterChip> chips = currentFilterChips();
    for (const FilterChip &chip : chips) {
        QWidget *widget = UiHelpers::makeChip(chip.label, chip.accessibleName, m_chipRow,
                                              chip.clear);
        m_chipLayout->insertWidget(m_chipLayout->count() - 1, widget);
        UiHelpers::animate(widget, UiHelpers::MotionKind::Chip);
    }
    m_chipRow->setVisible(!chips.isEmpty());
}

void MainWindow::clearTypeFilter()
{
    if (m_typeCombo)
        m_typeCombo->setCurrentIndex(0);
}

void MainWindow::clearDateFilter()
{
    if (m_dateCombo)
        m_dateCombo->setCurrentIndex(0);
}

void MainWindow::clearAppFilter()
{
    if (m_appCombo)
        m_appCombo->setCurrentIndex(0);
}

void MainWindow::clearTagFilter()
{
    if (m_tagCombo)
        m_tagCombo->setCurrentIndex(0);
}

void MainWindow::clearGroupFilter()
{
    m_groupFilter = 0;
    applyCurrentFilter();
}

void MainWindow::clearPinnedFilter()
{
    if (m_pinnedOnlyAction)
        m_pinnedOnlyAction->setChecked(false);
}

void MainWindow::clearSensitiveFilter()
{
    if (m_sensitiveAction)
        m_sensitiveAction->setChecked(false);
}

void MainWindow::clearSearchFilter()
{
    if (m_search) {
        m_search->clear();
        applyCurrentFilter();
    }
}

void MainWindow::clearAllFilters()
{
    // One refresh for the whole reset: block the per-control signals (each
    // combo/toggle otherwise re-runs the filter on its own), then apply once.
    // Sort is not a filter and is left alone.
    const QSignalBlocker typeBlocker(m_typeCombo);
    const QSignalBlocker dateBlocker(m_dateCombo);
    const QSignalBlocker appBlocker(m_appCombo);
    const QSignalBlocker tagBlocker(m_tagCombo);
    const QSignalBlocker pinnedBlocker(m_pinnedOnlyAction);
    const QSignalBlocker sensitiveBlocker(m_sensitiveAction);
    const QSignalBlocker timelineBlocker(m_timeline);
    if (m_typeCombo)
        m_typeCombo->setCurrentIndex(0);
    if (m_dateCombo)
        m_dateCombo->setCurrentIndex(0);
    if (m_appCombo)
        m_appCombo->setCurrentIndex(0);
    if (m_tagCombo)
        m_tagCombo->setCurrentIndex(0);
    m_groupFilter = 0;
    if (m_pinnedOnlyAction)
        m_pinnedOnlyAction->setChecked(false);
    if (m_sensitiveAction)
        m_sensitiveAction->setChecked(false);
    if (m_timeline)
        m_timeline->clearSelection();
    if (m_search)
        m_search->clear();
    applyCurrentFilter();
}

int MainWindow::activeFilterCount() const
{
    int count = 0;
    if (m_typeCombo && m_typeCombo->currentData().toInt() >= 0)
        ++count;
    if (m_dateCombo && m_dateCombo->currentData().toInt() != 0)
        ++count;
    if (m_appCombo && !m_appCombo->currentData().toString().isEmpty())
        ++count;
    if (m_tagCombo && !m_tagCombo->currentData().toString().isEmpty())
        ++count;
    if (m_groupFilter != 0)
        ++count;
    if (m_pinnedOnlyAction && m_pinnedOnlyAction->isChecked())
        ++count;
    if (m_sensitiveAction && m_sensitiveAction->isChecked())
        ++count;
    if (m_search && !m_search->text().trimmed().isEmpty())
        ++count;
    return count;
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    // Persist the session state while hidden: the next start restores it.
    if (m_ctx.settings()->rememberWindowGeometry()) {
        m_ctx.settings()->setWindowGeometry(saveGeometry());
        saveSplitterForMode();
        if (!m_previewDocked && m_splitter)
            m_ctx.settings()->setSplitterState(m_splitter->saveState());
    }
    if (m_ctx.settings()->restoreLastFilter() && m_model)
        m_ctx.settings()->setLastFilter(m_model->filter().toJsonString());
}
