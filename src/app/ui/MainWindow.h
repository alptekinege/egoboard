#pragma once

#include "DesignTokens.h"
#include "ExportImportDialogs.h"
#include "FilterSpec.h"

#include <QMainWindow>

#include <functional>

class ApplicationContext;
class ClipboardListModel;
class EntryDelegate;
class GroupsDock;
class PreviewPane;
class QAction;
class QComboBox;
class QDockWidget;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListView;
class QSplitter;
class QTimer;
class QToolBar;
class QToolButton;
class QVBoxLayout;
class CommandPalette;
class TimelineStrip;

// Two-pane history window: filter bar on top, entry list (infinite scroll) in
// the middle, full payload preview on the right; optional groups dock.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ApplicationContext &context, QWidget *parent = nullptr);

    void toggleVisibility();
    void applyCurrentFilter();
    void pasteEntry(qint64 entryId);
    void refreshAppFilter();
    void refreshTagFilter();
    void applySavedSearch(const FilterSpec &filter);
    void openSettings();
    void clearHistory();
    // Palette ">export [format]": opens the export dialog, preselected.
    void exportHistoryToFormat(const QString &format);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    // Keeps the empty-list hint the size of the list viewport.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void connectSignals();
    // Responsive shell (R1): Wide = list + preview side-by-side, Medium =
    // preview bottom drawer, Narrow = single pane + preview dialog/drawer.
    void applyResponsiveMode(int width);
    int shellModeIndex() const { return int(m_shellMode); }
    void saveSplitterForMode();
    void restoreSplitterForMode();
    void updateFilterBarMode();
    void updateToolbarOverflow();
    int activeFilterCount() const;
    // R2: removable filter chips, bulk-action bar, day headers.
    void rebuildFilterChips();
    struct FilterChip {
        QString label;
        QString accessibleName;
        std::function<void()> clear;
    };
    QVector<FilterChip> currentFilterChips();
    void clearTypeFilter();
    void clearDateFilter();
    void clearAppFilter();
    void clearTagFilter();
    void clearGroupFilter();
    void clearPinnedFilter();
    void clearSensitiveFilter();
    void clearSearchFilter();
    void updateBulkBar();
    void bulkPin(bool pinned);
    void bulkTag();
    void bulkMoveToGroup();
    void bulkExport();
    void bulkDelete();
    void showUndoToast(const QString &message, const QVector<ClipboardRecord> &deleted);
    void updateEmptyState(); // "no entries yet" vs "nothing matches this filter"
    void onSelectionChanged();
    void onActivated(const QModelIndex &index);
    void pasteCurrent();
    void copyCurrent();
    void showScopeMenu();
    void showRecentSearches();
    // Records the current search box text in the recent-searches list.
    void commitCurrentSearch();
    void deleteSelected();
    void deleteFiltered(); // bulk delete of everything matching the current filter
    void togglePinSelected();
    void showContextMenu(const QPoint &pos);
    void buildSavedSearchesMenu();
    void updateActionStates();
    void openPalette();
    void openSnippetDialog();
    void openTransformChain();
    void repositionCenteredOnActiveScreen();

    ApplicationContext &m_ctx;
    ClipboardListModel *m_model = nullptr;
    EntryDelegate *m_delegate = nullptr;
    PreviewPane *m_preview = nullptr;
    GroupsDock *m_groupsDock = nullptr;
    QListView *m_list = nullptr;
    QLineEdit *m_search = nullptr;
    QAction *m_scopeAction = nullptr;
    QAction *m_recentSearchAction = nullptr;
    int m_searchScope = 0; // FilterSpec::SearchScope value
    QLabel *m_queryHint = nullptr;
    QWidget *m_chipRow = nullptr; // R2: removable active-filter chips
    QHBoxLayout *m_chipLayout = nullptr;
    QWidget *m_bulkBar = nullptr; // R2: bulk actions for multi-select
    QLabel *m_bulkCount = nullptr;
    QLabel *m_emptyHint = nullptr; // over the list viewport while it has no rows
    QWidget *m_listSkeleton = nullptr; // U11 shimmer rows while the first page loads
    void showListSkeleton();
    void hideListSkeleton();
    QComboBox *m_typeCombo = nullptr;
    QComboBox *m_dateCombo = nullptr;
    QComboBox *m_appCombo = nullptr;
    QComboBox *m_tagCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QSplitter *m_splitter = nullptr;
    QToolButton *m_savedSearchesButton = nullptr;
    QTimer *m_searchDebounce = nullptr;
    // Responsive shell (R1): filter rows, preview dock, overflow menu.
    QWidget *m_filterWidget = nullptr; // search row
    QWidget *m_filterRow = nullptr; // combo row, collapses into m_filtersButton
    QToolButton *m_filtersButton = nullptr; // "Filters (n)" under Medium/Narrow
    QDockWidget *m_previewDock = nullptr; // Medium: bottom drawer hosting m_preview
    bool m_previewDocked = false; // true while m_preview lives in m_previewDock
    QWidget *m_central = nullptr;
    QVBoxLayout *m_centralLayout = nullptr;
    DesignTokens::ShellMode m_shellMode = DesignTokens::ShellMode::Wide;

    // toolbar / context actions
    QToolBar *m_toolbar = nullptr;
    QAction *m_pinAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_groupsAction = nullptr;
    QAction *m_pinnedOnlyAction = nullptr;
    QAction *m_sensitiveAction = nullptr; // audit view: sensitive entries only
    QAction *m_deleteFilteredAction = nullptr;
    QToolButton *m_moreButton = nullptr; // "More" overflow under Medium/Narrow
    QVector<QAction *> m_overflowActions; // moved into m_moreButton off Wide

    qint64 m_selectedId = 0;
    qint64 m_groupFilter = 0; // 0 = all
    ExportImportDialogs::DateRange m_lastRange;
    CommandPalette *m_palette = nullptr;
    TimelineStrip *m_timeline = nullptr;
};
