#pragma once

#include "ExportImportDialogs.h"
#include "FilterSpec.h"

#include <QMainWindow>

class ApplicationContext;
class ClipboardListModel;
class EntryDelegate;
class GroupsDock;
class PreviewPane;
class QComboBox;
class QLineEdit;
class QListView;
class QSplitter;
class QTimer;
class QToolBar;
class QToolButton;
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

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void buildUi();
    void connectSignals();
    void onSelectionChanged();
    void onActivated(const QModelIndex &index);
    void pasteCurrent();
    void copyCurrent();
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
    QComboBox *m_typeCombo = nullptr;
    QComboBox *m_dateCombo = nullptr;
    QComboBox *m_appCombo = nullptr;
    QComboBox *m_tagCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QSplitter *m_splitter = nullptr;
    QToolButton *m_savedSearchesButton = nullptr;
    QTimer *m_searchDebounce = nullptr;

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

    qint64 m_selectedId = 0;
    qint64 m_groupFilter = 0; // 0 = all
    ExportImportDialogs::DateRange m_lastRange;
    CommandPalette *m_palette = nullptr;
    TimelineStrip *m_timeline = nullptr;
};
