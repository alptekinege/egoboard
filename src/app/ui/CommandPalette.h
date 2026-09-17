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
class SnippetManager;
class ScriptActionManager;

// Floating command palette: Ctrl+K to fuzzy-search history and execute actions.
// Local-only, no network. Supports plain text search (FTS5-backed) and a small
// command set prefixed with '>' (>pin, >copy, >delete, >transform, >snippet).
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(IClipboardStorage *storage, QWidget *parent = nullptr);

    void openPalette();
    void setSnippetManager(SnippetManager *m) { m_snippets = m; }
    void setScriptManager(ScriptActionManager *m) { m_scripts = m; }

signals:
    void pasteRequested(qint64 entryId);
    // entryId 0 = the entry selected in the main window.
    void copyRequested(qint64 entryId);
    void pinRequested(qint64 entryId);
    void transformRequested(const QString &transformName, qint64 entryId);
    void snippetRequested(qint64 snippetId, qint64 entryId);

private slots:
    void onTextChanged(const QString &text);
    void onActivated(const QModelIndex &index);
    void executeCurrent();

private:
    void refreshResults(const QString &query);
    void updateHint();
    static int fuzzyScore(const QString &query, const QString &candidate);

    enum class Mode { History, Transforms, Snippets, Command };
    Mode m_mode = Mode::History;
    QString m_pendingCommand; // set in Mode::Command ("copy" / "pin")

    IClipboardStorage *m_storage = nullptr;
    SnippetManager *m_snippets = nullptr;
    ScriptActionManager *m_scripts = nullptr;
    QLineEdit *m_input = nullptr;
    QListView *m_list = nullptr;
    QLabel *m_hint = nullptr;

    // Lightweight model for palette rows (keeps ClipboardRecord vector).
    class PaletteModel;
    PaletteModel *m_model = nullptr;
    QVector<ClipboardRecord> m_results;
    // For transform/snippet modes we reuse model but store names/ids in separate vectors
    struct TransformItem { QString name; QString label; QString desc; };
    QVector<TransformItem> m_transformItems;
    struct SnippetItem { qint64 id; QString name; QString templateText; };
    QVector<SnippetItem> m_snippetItems;
    QString m_currentQuery;
};
