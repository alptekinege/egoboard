#pragma once

#include "ClipboardRecord.h"
#include "FilterSpec.h"

#include <QDialog>
#include <QVector>

class IClipboardStorage;
class QListView;
class QLineEdit;
class QLabel;
class QAbstractListModel;

// Floating command palette: Ctrl+K to fuzzy-search history and execute actions.
// Local-only, no network. Supports plain text search (FTS5-backed) and a small
// command set prefixed with '>' (>pin, >copy, >delete).
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(IClipboardStorage *storage, QWidget *parent = nullptr);

    void openPalette();

signals:
    void pasteRequested(qint64 entryId);
    void copyRequested(qint64 entryId);

private slots:
    void onTextChanged(const QString &text);
    void onActivated(const QModelIndex &index);
    void executeCurrent();

private:
    void refreshResults(const QString &query);
    void updateHint();
    static int fuzzyScore(const QString &query, const QString &candidate);

    IClipboardStorage *m_storage = nullptr;
    QLineEdit *m_input = nullptr;
    QListView *m_list = nullptr;
    QLabel *m_hint = nullptr;

    // Lightweight model for palette rows (keeps ClipboardRecord vector).
    class PaletteModel;
    PaletteModel *m_model = nullptr;
    QVector<ClipboardRecord> m_results;
    QString m_currentQuery;
};
