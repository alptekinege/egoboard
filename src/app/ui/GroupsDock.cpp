#include "GroupsDock.h"

#include "DesignTokens.h"
#include "GroupTreeModel.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

// Group colors default to the scheme's own accent rather than a fixed blue.
QColor defaultGroupColor()
{
    return QApplication::palette().color(QPalette::Highlight);
}

// A short curated list of Breeze icon names suitable for groups; editable.
const char *kIconPresets[] = {
    "folder",       "bookmarks",   "edit-paste",  "favorite",    "starred",
    "view-calendar", "mail-message", "web-browser", "dialog-password", "development",
    "folder-documents", "folder-download", "media-playback-start", "tag", "flag",
};

// Tree view that marks the group the dragged entries would land on, so the
// drop target is visible before the mouse button is released.
class DropTargetTreeView : public QTreeView {
public:
    using QTreeView::QTreeView;

protected:
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        setDropIndex(indexAt(event->position().toPoint()));
        QTreeView::dragMoveEvent(event);
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        setDropIndex({});
        QTreeView::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        setDropIndex({});
        QTreeView::dropEvent(event);
    }

    void drawRow(QPainter *painter, const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override
    {
        if (index == m_dropIndex) {
            QColor wash = palette().color(QPalette::Highlight);
            wash.setAlpha(DesignTokens::DropTargetAlpha);
            painter->fillRect(option.rect, wash);
        }
        QTreeView::drawRow(painter, option, index);
    }

private:
    void setDropIndex(const QModelIndex &index)
    {
        if (m_dropIndex == index)
            return;
        m_dropIndex = index;
        viewport()->update();
    }

    QModelIndex m_dropIndex;
};

class GroupDialog : public QDialog {
public:
    GroupDialog(QWidget *parent, const QString &title, const QString &name, const QColor &color,
                const QString &icon)
        : QDialog(parent)
    {
        setWindowTitle(title);
        auto *layout = new QVBoxLayout(this);

        auto *nameRow = new QHBoxLayout();
        nameRow->addWidget(new QLabel(tr("Name:"), this));
        m_nameEdit = new QLineEdit(name, this);
        m_nameEdit->setFocus();
        nameRow->addWidget(m_nameEdit, 1);
        layout->addLayout(nameRow);

        auto *colorRow = new QHBoxLayout();
        colorRow->addWidget(new QLabel(tr("Color:"), this));
        auto *colorButton = new QPushButton(tr("Pick…"), this);
        m_color = color.isValid() ? color : QColor();
        updateColorButton(colorButton);
        connect(colorButton, &QPushButton::clicked, this, [this, colorButton] {
            const QColor picked =
                QColorDialog::getColor(m_color.isValid() ? m_color : defaultGroupColor(),
                                       this, tr("Group color"));
            if (picked.isValid()) {
                m_color = picked;
                updateColorButton(colorButton);
            }
        });
        colorRow->addWidget(colorButton);
        auto *clearColor = new QPushButton(tr("None"), this);
        connect(clearColor, &QPushButton::clicked, this, [this, colorButton] {
            m_color = QColor();
            updateColorButton(colorButton);
        });
        colorRow->addWidget(clearColor);
        colorRow->addStretch(1);
        layout->addLayout(colorRow);

        auto *iconRow = new QHBoxLayout();
        iconRow->addWidget(new QLabel(tr("Icon:"), this));
        m_iconCombo = new QComboBox(this);
        m_iconCombo->setEditable(true);
        for (const char *preset : kIconPresets)
            m_iconCombo->addItem(QIcon::fromTheme(QString::fromLatin1(preset)),
                                 QString::fromLatin1(preset));
        m_iconCombo->setCurrentText(icon.isEmpty() ? QStringLiteral("folder") : icon);
        iconRow->addWidget(m_iconCombo, 1);
        layout->addLayout(iconRow);

        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        connect(m_nameEdit, &QLineEdit::textChanged, this, [this, buttons](const QString &text) {
            buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
        });
    }

    QString groupName() const { return m_nameEdit->text().trimmed(); }
    QColor color() const { return m_color; }
    QString iconName() const { return m_iconCombo->currentText().trimmed(); }

private:
    void updateColorButton(QPushButton *button)
    {
        button->setText(m_color.isValid() ? m_color.name() : tr("None"));
    }
    QLineEdit *m_nameEdit = nullptr;
    QColor m_color;
    QComboBox *m_iconCombo = nullptr;
};

} // namespace

GroupsDock::GroupsDock(BookmarkManager *bookmarks, QWidget *parent)
    : QDockWidget(tr("Favorites & Groups"), parent)
    , m_bookmarks(bookmarks)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(DockWidgetMovable | DockWidgetClosable);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto *toolbar = new QToolBar(container);
    toolbar->setIconSize(QSize(16, 16));
    QAction *newAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                            tr("New group"));
    connect(newAction, &QAction::triggered, this, &GroupsDock::newGroup);
    QAction *editAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("configure")),
                                             tr("Edit"));
    connect(editAction, &QAction::triggered, this, &GroupsDock::editSelected);
    QAction *deleteAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("list-remove")),
                                               tr("Delete"));
    connect(deleteAction, &QAction::triggered, this, &GroupsDock::deleteSelected);
    layout->addWidget(toolbar);

    m_model = new GroupTreeModel(bookmarks, this);
    connect(m_model, &GroupTreeModel::entriesDropped, this, &GroupsDock::entriesDropped);

    m_tree = new DropTargetTreeView(container);
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &GroupsDock::onSelectionChanged);
    layout->addWidget(m_tree, 1);

    auto *showAll = new QPushButton(tr("Show all entries"), container);
    connect(showAll, &QPushButton::clicked, this, [this] { emit groupSelected(0); });
    layout->addWidget(showAll);

    setWidget(container);
}

void GroupsDock::newGroup()
{
    // Parent = currently selected group (top level when nothing selected).
    qint64 parentId = 0;
    const QModelIndex current = m_tree->currentIndex();
    if (current.isValid()) {
        const auto parent = m_model->groupForIndex(m_model->parent(current));
        if (parent.has_value())
            parentId = parent->id;
        else {
            const auto group = m_model->groupForIndex(current);
            if (group.has_value())
                parentId = group->id;
        }
    }

    GroupDialog dialog(this, tr("New group"), QString(), QColor(), QString());
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (m_bookmarks->createGroup(dialog.groupName(), parentId,
                                 dialog.color().isValid() ? dialog.color().name() : QString(),
                                 dialog.iconName())
        == 0) {
        QMessageBox::warning(this, tr("New group"),
                             tr("A group named “%1” already exists here.").arg(dialog.groupName()));
    }
}

void GroupsDock::editSelected()
{
    const QModelIndex current = m_tree->currentIndex();
    const auto group = m_model->groupForIndex(current);
    if (!group.has_value())
        return;

    GroupDialog dialog(this, tr("Edit group"), group->name, QColor(group->color), group->icon);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!m_bookmarks->updateGroup(group->id, dialog.groupName(),
                                  dialog.color().isValid() ? dialog.color().name() : QString(),
                                  dialog.iconName())) {
        QMessageBox::warning(this, tr("Edit group"),
                             tr("A group named “%1” already exists here.").arg(dialog.groupName()));
    }
}

void GroupsDock::deleteSelected()
{
    const QModelIndex current = m_tree->currentIndex();
    const auto group = m_model->groupForIndex(current);
    if (!group.has_value())
        return;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Delete group"),
        tr("Delete group “%1”?\nIts entries stay in history; child groups move one level up.")
            .arg(group->name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        m_bookmarks->deleteGroup(group->id);
}

void GroupsDock::onSelectionChanged()
{
    const QModelIndex current = m_tree->currentIndex();
    const auto group = m_model->groupForIndex(current);
    emit groupSelected(group.has_value() ? group->id : qint64(0));
}
