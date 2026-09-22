#include "ExportImportDialogs.h"

#include "BookmarkManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
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

    auto *formatRow = new QHBoxLayout();
    formatRow->addWidget(new QLabel(tr("Format:"), this));
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(tr("JSON — full backup, can be imported again"),
                           int(ExportImportManager::ExportFormat::Json));
    m_formatCombo->addItem(tr("Markdown — readable document"), int(ExportImportManager::ExportFormat::Markdown));
    m_formatCombo->addItem(tr("CSV — spreadsheet"), int(ExportImportManager::ExportFormat::Csv));
    m_formatCombo->addItem(tr("HTML — shareable page"), int(ExportImportManager::ExportFormat::Html));
    m_formatCombo->setToolTip(tr("Only JSON files can be imported back into Egoboard; the other "
                                 "formats export the entries for reading and sharing."));
    formatRow->addWidget(m_formatCombo, 1);
    layout->addLayout(formatRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ExportDialog::setFormat(ExportImportManager::ExportFormat format)
{
    const int index = m_formatCombo->findData(int(format));
    if (index >= 0)
        m_formatCombo->setCurrentIndex(index);
}

void ExportDialog::pickPath()
{
    QString filter = tr("Egoboard export (*.json);;All files (*)");
    QString suggested = QStringLiteral("egoboard-export.json");
    switch (format()) {
    case ExportImportManager::ExportFormat::Markdown:
        filter = tr("Markdown (*.md);;All files (*)");
        suggested = QStringLiteral("egoboard-history.md");
        break;
    case ExportImportManager::ExportFormat::Csv:
        filter = tr("CSV (*.csv);;All files (*)");
        suggested = QStringLiteral("egoboard-history.csv");
        break;
    case ExportImportManager::ExportFormat::Html:
        filter = tr("HTML (*.html);;All files (*)");
        suggested = QStringLiteral("egoboard-history.html");
        break;
    case ExportImportManager::ExportFormat::Json:
        break;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export to file"), suggested, filter);
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

ExportImportManager::ExportFormat ExportDialog::format() const
{
    return static_cast<ExportImportManager::ExportFormat>(m_formatCombo->currentData().toInt());
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

ImageExportDialog::ImageExportDialog(BookmarkManager *bookmarks, const QList<qint64> &selectedIds,
                                     const FilterSpec &currentFilter, bool filterActive,
                                     QWidget *parent)
    : QDialog(parent)
{
    Q_UNUSED(currentFilter);
    setWindowTitle(tr("Export images"));

    auto *layout = new QVBoxLayout(this);

    auto *scopeBox = new QGroupBox(tr("Which images"), this);
    auto *scopeLayout = new QVBoxLayout(scopeBox);
    m_selectionRadio = new QRadioButton(
        tr("Selected entries (%n image candidates)", nullptr, selectedIds.size()), scopeBox);
    m_selectionRadio->setEnabled(!selectedIds.isEmpty());
    m_filterRadio = new QRadioButton(tr("Current list filter"), scopeBox);
    m_filterRadio->setEnabled(filterActive);
    m_allRadio = new QRadioButton(tr("Everything (all images in history)"), scopeBox);
    m_pinnedRadio = new QRadioButton(tr("Pinned entries only"), scopeBox);
    m_groupRadio = new QRadioButton(tr("A group and its subgroups:"), scopeBox);
    m_groupCombo = new QComboBox(scopeBox);
    for (const BookmarkGroup &group : bookmarks->groups())
        m_groupCombo->addItem(group.name, group.id);
    m_groupCombo->setEnabled(false);
    connect(m_groupRadio, &QRadioButton::toggled, m_groupCombo, &QComboBox::setEnabled);
    scopeLayout->addWidget(m_selectionRadio);
    scopeLayout->addWidget(m_filterRadio);
    scopeLayout->addWidget(m_allRadio);
    scopeLayout->addWidget(m_pinnedRadio);
    scopeLayout->addWidget(m_groupRadio);
    scopeLayout->addWidget(m_groupCombo);
    layout->addWidget(scopeBox);

    auto *folderRow = new QHBoxLayout();
    folderRow->addWidget(new QLabel(tr("Folder:"), this));
    m_folderEdit = new QLineEdit(this);
    m_folderEdit->setPlaceholderText(tr("/path/to/egoboard-images"));
    auto *browse = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-open")),
                                   tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &ImageExportDialog::pickFolder);
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(browse);
    layout->addLayout(folderRow);

    m_sensitiveCheck = new QCheckBox(tr("Include entries flagged sensitive"), this);
    m_sensitiveCheck->setToolTip(tr("When unchecked (default), sensitive images are skipped "
                                    "and reported instead of written."));
    layout->addWidget(m_sensitiveCheck);
    m_textCheck = new QCheckBox(tr("Include text payloads in manifest.json"), this);
    m_textCheck->setToolTip(tr("The manifest otherwise carries metadata only (source app, "
                               "time, tags, OCR text). Check this only when a local tool "
                               "needs the payloads for indexing."));
    layout->addWidget(m_textCheck);

    auto *hint = new QLabel(tr("Writes one PNG per stored image plus <code>manifest.json</code> "
                               "(source app, capture time, tags, OCR text). Entries without a "
                               "stored image are skipped and reported. Existing files are never "
                               "overwritten; canceling writes no manifest."), this);
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Default to the widest sensible scope: the selection when there is one,
    // otherwise the whole history (callers override via setScope).
    if (!selectedIds.isEmpty())
        m_selectionRadio->setChecked(true);
    else
        m_allRadio->setChecked(true);
}

void ImageExportDialog::setScope(Scope scope)
{
    QRadioButton *target = m_allRadio;
    switch (scope) {
    case Scope::Selection:
        target = m_selectionRadio->isEnabled() ? m_selectionRadio : m_allRadio;
        break;
    case Scope::CurrentFilter:
        target = m_filterRadio->isEnabled() ? m_filterRadio : m_allRadio;
        break;
    case Scope::PinnedOnly:
        target = m_pinnedRadio;
        break;
    case Scope::GroupSubtree:
        target = m_groupRadio;
        break;
    case Scope::Everything:
        target = m_allRadio;
        break;
    }
    target->setChecked(true);
}

void ImageExportDialog::pickFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Image export folder"),
                                                          m_folderEdit->text().trimmed());
    if (!dir.isEmpty())
        m_folderEdit->setText(dir);
}

ImageExportDialog::Scope ImageExportDialog::scope() const
{
    if (m_selectionRadio->isChecked())
        return Scope::Selection;
    if (m_filterRadio->isChecked())
        return Scope::CurrentFilter;
    if (m_pinnedRadio->isChecked())
        return Scope::PinnedOnly;
    if (m_groupRadio->isChecked())
        return Scope::GroupSubtree;
    return Scope::Everything;
}

qint64 ImageExportDialog::groupId() const
{
    return m_groupCombo->currentData().toLongLong();
}

QString ImageExportDialog::folder() const
{
    return m_folderEdit->text().trimmed();
}

bool ImageExportDialog::includeSensitive() const
{
    return m_sensitiveCheck->isChecked();
}

bool ImageExportDialog::includeText() const
{
    return m_textCheck->isChecked();
}

} // namespace ExportImportDialogs
