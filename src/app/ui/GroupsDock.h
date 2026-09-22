#pragma once

#include "BookmarkManager.h"

#include <QDockWidget>

class GroupTreeModel;
class QTreeView;

// Left dock: the favorites group tree. Supports create/rename/delete/recolor/
// re-icon groups, drag entries from the history list onto a group, and
// drag groups onto other groups to re-parent them.
class GroupsDock : public QDockWidget {
    Q_OBJECT
public:
    explicit GroupsDock(BookmarkManager *bookmarks, QWidget *parent = nullptr);

    signals:
        void groupSelected(qint64 groupId); // 0 = show everything
        void entriesDropped(const QList<qint64> &entryIds, qint64 groupId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void newGroup();
    void editSelected(); // rename + color + icon
    void deleteSelected();
    void onSelectionChanged();
    void updateEmptyState(); // U13 empty state

    BookmarkManager *m_bookmarks = nullptr;
    GroupTreeModel *m_model = nullptr;
    QTreeView *m_tree = nullptr;
    QWidget *m_emptyState = nullptr; // U13 empty state overlay
};
