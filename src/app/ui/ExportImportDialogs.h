#pragma once

#include "ExportImportManager.h"

#include <QDialog>

class QComboBox;
class QDateEdit;
class QLineEdit;
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
    qint64 groupId() const;

private:
    void updatePath();
    void pickPath();

    QLineEdit *m_pathEdit = nullptr;
    QRadioButton *m_allRadio = nullptr;
    QRadioButton *m_pinnedRadio = nullptr;
    QRadioButton *m_groupRadio = nullptr;
    QComboBox *m_groupCombo = nullptr;
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

} // namespace ExportImportDialogs
