#pragma once

#include "ExportImportManager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QListWidget;
class QRadioButton;

// Small dialogs for JSON export/import and custom date-range filtering.
namespace ExportImportDialogs {

struct DateRange {
    bool isValid = false;
    qint64 fromMs = 0;
    qint64 toMs = 0;
};

class DateRangeDialog : public QDialog {
    Q_OBJECT
public:
    explicit DateRangeDialog(QWidget *parent = nullptr);

    DateRange range() const;

private:
    QDateEdit *m_from = nullptr;
    QDateEdit *m_to = nullptr;
};

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    enum Scope {
        Everything,
        PinnedOnly,
        GroupSubtree,
    };

    ExportDialog(class BookmarkManager *bookmarks, QWidget *parent);

    QString filePath() const;
    Scope scope() const;
    ExportImportManager::ExportFormat format() const;
    qint64 groupId() const;
    // Preselects a format (used by the palette's ">export markdown").
    void setFormat(ExportImportManager::ExportFormat format);

private:
    void pickPath();

    QLineEdit *m_pathEdit = nullptr;
    QRadioButton *m_allRadio = nullptr;
    QRadioButton *m_pinnedRadio = nullptr;
    QRadioButton *m_groupRadio = nullptr;
    QComboBox *m_groupCombo = nullptr;
    QComboBox *m_formatCombo = nullptr;
};

// Image-only export (U17): dumps the stored PNG blobs of a scope into a
// folder plus a small manifest.json (metadata only, unless text is opted in).
// Entries without a stored blob are skipped and reported, never written as
// empty files; existing files are never overwritten.
class ImageExportDialog : public QDialog {
    Q_OBJECT
public:
    using Scope = ExportImportManager::ImageExportRequest::Scope;

    ImageExportDialog(class BookmarkManager *bookmarks, const QList<qint64> &selectedIds,
                      const FilterSpec &currentFilter, bool filterActive, QWidget *parent);

    Scope scope() const;
    qint64 groupId() const;
    QString folder() const;
    bool includeSensitive() const;
    bool includeText() const;
    // Preselects a scope (the bulk bar starts on Selection, ">export images"
    // on the current filter).
    void setScope(Scope scope);

private:
    void pickFolder();

    QRadioButton *m_selectionRadio = nullptr;
    QRadioButton *m_filterRadio = nullptr;
    QRadioButton *m_allRadio = nullptr;
    QRadioButton *m_pinnedRadio = nullptr;
    QRadioButton *m_groupRadio = nullptr;
    QComboBox *m_groupCombo = nullptr;
    QLineEdit *m_folderEdit = nullptr;
    QCheckBox *m_sensitiveCheck = nullptr;
    QCheckBox *m_textCheck = nullptr;
};

class ImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImportDialog(QWidget *parent);

    QString filePath() const;
    ExportImportManager::ImportMode mode() const;

private:
    void pickPath();

    QLineEdit *m_pathEdit = nullptr;
    QRadioButton *m_mergeRadio = nullptr;
    QRadioButton *m_overwriteRadio = nullptr;
    QRadioButton *m_skipRadio = nullptr;
};

// Picks one of the automatic backups (newest first) and how to apply it.
class RestoreBackupDialog : public QDialog {
    Q_OBJECT
public:
    RestoreBackupDialog(const QStringList &backupPaths, QWidget *parent);

    QString selectedPath() const;
    ExportImportManager::ImportMode mode() const;

private:
    QListWidget *m_list = nullptr;
    QRadioButton *m_overwriteRadio = nullptr;
    QRadioButton *m_mergeRadio = nullptr;
};

} // namespace ExportImportDialogs
