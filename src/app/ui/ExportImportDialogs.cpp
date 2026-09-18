#include "ExportImportDialogs.h"

#include "BookmarkManager.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace ExportImportDialogs {

DateRangeDialog::DateRangeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Custom date range"));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_from = new QDateEdit(QDate::currentDate().addDays(-7), this);
    m_from->setCalendarPopup(true);
    m_from->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_to = new QDateEdit(QDate::currentDate(), this);
    m_to->setCalendarPopup(true);
    m_to->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));

    form->addRow(tr("From:"), m_from);
    form->addRow(tr("To:"), m_to);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

DateRange DateRangeDialog::range() const
{
    DateRange result;
    if (m_from->date() > m_to->date())
        return result;
    result.isValid = true;
    result.fromMs = QDateTime(m_from->date(), QTime(0, 0)).toMSecsSinceEpoch();
    result.toMs = QDateTime(m_to->date(), QTime(23, 59, 59)).toMSecsSinceEpoch();
    return result;
}

ExportDialog::ExportDialog(BookmarkManager *bookmarks, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export history"));

    auto *layout = new QVBoxLayout(this);

    auto *scopeBox = new QGroupBox(tr("What to export"), this);
    auto *scopeLayout = new QVBoxLayout(scopeBox);
    m_allRadio = new QRadioButton(tr("Everything (all entries and groups)"), scopeBox);
    m_allRadio->setChecked(true);
    m_pinnedRadio = new QRadioButton(tr("Pinned entries only"), scopeBox);
    m_groupRadio = new QRadioButton(tr("A group and its subgroups:"), scopeBox);
    m_groupCombo = new QComboBox(scopeBox);
    for (const BookmarkGroup &group : bookmarks->groups())
        m_groupCombo->addItem(group.name, group.id);
    m_groupCombo->setEnabled(false);
    connect(m_groupRadio, &QRadioButton::toggled, m_groupCombo, &QComboBox::setEnabled);
    scopeLayout->addWidget(m_allRadio);
    scopeLayout->addWidget(m_pinnedRadio);
    scopeLayout->addWidget(m_groupRadio);
    scopeLayout->addWidget(m_groupCombo);
    layout->addWidget(scopeBox);

    auto *pathRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("/path/to/egoboard-export.json"));
    auto *browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &ExportDialog::pickPath);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ExportDialog::pickPath()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export to file"), QStringLiteral("egoboard-export.json"),
        tr("Egoboard export (*.json);;All files (*)"));
    if (!path.isEmpty())
        m_pathEdit->setText(path);
}

QString ExportDialog::filePath() const
{
    return m_pathEdit->text().trimmed();
}

ExportDialog::Scope ExportDialog::scope() const
{
    if (m_pinnedRadio->isChecked())
        return PinnedOnly;
    if (m_groupRadio->isChecked())
        return GroupSubtree;
    return Everything;
}

qint64 ExportDialog::groupId() const
{
    return m_groupCombo->currentData().toLongLong();
}

ImportDialog::ImportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import history"));

    auto *layout = new QVBoxLayout(this);

    auto *pathRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("/path/to/egoboard-export.json"));
    auto *browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &ImportDialog::pickPath);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);

    auto *modeBox = new QGroupBox(tr("Conflict handling"), this);
    auto *modeLayout = new QVBoxLayout(modeBox);
    m_mergeRadio = new QRadioButton(
        tr("Merge — keep the newer copy of duplicates and combine groups"), modeBox);
    m_mergeRadio->setChecked(true);
    m_overwriteRadio =
        new QRadioButton(tr("Overwrite — replace the whole local history"), modeBox);
    m_skipRadio = new QRadioButton(tr("Skip — only add entries not in the local history"),
                                   modeBox);
    modeLayout->addWidget(m_mergeRadio);
    modeLayout->addWidget(m_overwriteRadio);
    modeLayout->addWidget(m_skipRadio);
    layout->addWidget(modeBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ImportDialog::pickPath()
{
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Import from file"), QString(),
                                     tr("Egoboard export (*.json);;All files (*)"));
    if (!path.isEmpty())
        m_pathEdit->setText(path);
}

QString ImportDialog::filePath() const
{
    return m_pathEdit->text().trimmed();
}

ExportImportManager::ImportMode ImportDialog::mode() const
{
    if (m_overwriteRadio->isChecked())
        return ExportImportManager::ImportMode::Overwrite;
    if (m_skipRadio->isChecked())
        return ExportImportManager::ImportMode::SkipDuplicates;
    return ExportImportManager::ImportMode::Merge;
}

RestoreBackupDialog::RestoreBackupDialog(const QStringList &backupPaths, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Restore from backup"));

    auto *layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);
    for (const QString &path : backupPaths) {
        const QFileInfo info(path);
        const QString when = info.lastModified().toString(
            QLocale::system().dateTimeFormat(QLocale::ShortFormat));
        auto *item = new QListWidgetItem(
            tr("%1 — %2").arg(when, QLocale::system().formattedDataSize(info.size())), m_list);
        item->setData(Qt::UserRole, path);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0); // newest first
    layout->addWidget(m_list, 1);

    auto *modeBox = new QGroupBox(tr("How to apply it"), this);
    auto *modeLayout = new QVBoxLayout(modeBox);
    m_overwriteRadio = new QRadioButton(
        tr("Replace the current history with the backup"), modeBox);
    m_overwriteRadio->setChecked(true);
    m_mergeRadio = new QRadioButton(
        tr("Merge — keep the newer copy of duplicates and combine groups"), modeBox);
    modeLayout->addWidget(m_overwriteRadio);
    modeLayout->addWidget(m_mergeRadio);
    layout->addWidget(modeBox);
    layout->addWidget(new QLabel(tr("Replacing cannot be undone; the current history is not "
                                    "backed up automatically before a restore."),
                                 this));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString RestoreBackupDialog::selectedPath() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

ExportImportManager::ImportMode RestoreBackupDialog::mode() const
{
    return m_overwriteRadio->isChecked() ? ExportImportManager::ImportMode::Overwrite
                                         : ExportImportManager::ImportMode::Merge;
}

} // namespace ExportImportDialogs
