#pragma once

#include "ClipboardRecord.h"
#include "FilterSpec.h"
#include "PaletteCommands.h"
#include "SearchEngine.h"

#include <QDialog>
#include <QVector>

class IClipboardStorage;
class QListView;
class QLineEdit;
class QLabel;
class QFrame;
class QAbstractListModel;
class QStyledItemDelegate;
class SnippetManager;
class ScriptActionManager;

// Floating command palette: Ctrl+K to fuzzy-search history and execute actions.
// Local-only, no network. Supports plain text search (FTS5-backed) and the
// command set described by PaletteCommands ('>' prefix, argument completion
// through Tab or a second Enter, recent commands remembered).
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(IClipboardStorage *storage, QWidget *parent = nullptr);

    void openPalette();
    void setSnippetManager(SnippetManager *m) { m_snippets = m; }
    void setScriptManager(ScriptActionManager *m) { m_scripts = m; }
    // Candidates for argument completion, refreshed by the window before opening.
    void setTagCandidates(const QStringList &tags) { m_tagCandidates = tags; }
    void setGroupCandidates(const QStringList &groups) { m_groupCandidates = groups; }
    // Ids of the commands used before, most recent first.
    void setRecentCommands(const QStringList &ids) { m_recentCommands = ids; }

signals:
    void pasteRequested(qint64 entryId);
    // entryId 0 = the entry selected in the main window.
    void copyRequested(qint64 entryId);
    void pinRequested(qint64 entryId);
    void transformRequested(const QString &transformName, qint64 entryId);
    void snippetRequested(qint64 snippetId, qint64 entryId);
    // Commands that act on the main window's selection.
    void deleteRequested();
    void tagRequested(const QString &tag);
    void groupRequested(const QString &group);
    // Empty format = let the window ask (export dialog).
    void exportRequested(const QString &format);
    void togglePauseRequested();
    void settingsRequested();
    void clearHistoryRequested();
    // Emitted for every executed command so the window can remember it.
    void commandExecuted(const QString &commandId);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    // Tab is consumed by the line edit's focus handling before the dialog sees
    // it, so completion listens on the input directly.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onTextChanged(const QString &text);
    void onActivated(const QModelIndex &index);
    void executeCurrent();

private:
    void refreshResults(const QString &query);
    void updateHint();
    void runCommand(const PaletteCommands::Command &command, const QString &argument);
    // Replaces the argument part of the input with the highlighted candidate.
    bool completeFromSelection();
    static int fuzzyScore(const QString &query, const QString &candidate);

    enum class Mode { History, Transforms, Snippets, Commands, Argument, Command };
    Mode m_mode = Mode::History;
    const PaletteCommands::Command *m_pending = nullptr; // Mode::Command / Argument

    IClipboardStorage *m_storage = nullptr;
    SnippetManager *m_snippets = nullptr;
    ScriptActionManager *m_scripts = nullptr;
    QLineEdit *m_input = nullptr;
    QListView *m_list = nullptr;
    QLabel *m_hint = nullptr;
    QFrame *m_card = nullptr; // rounded, shadowed panel inside the frameless window

    // Lightweight model for palette rows (keeps ClipboardRecord vector).
    class PaletteModel;
    class PaletteDelegate;
    PaletteModel *m_model = nullptr;
    PaletteDelegate *m_delegate = nullptr;
    QVector<ClipboardRecord> m_results;
    // For transform/snippet modes we reuse model but store names/ids in separate vectors
    struct TransformItem { QString name; QString label; QString desc; };
    QVector<TransformItem> m_transformItems;
    struct SnippetItem { qint64 id; QString name; QString templateText; };
    QVector<SnippetItem> m_snippetItems;
    struct CommandItem { QString id; QString usage; QString description; };
    QVector<CommandItem> m_commandItems;
    QStringList m_argumentItems;
    QStringList m_tagCandidates;
    QStringList m_groupCandidates;
    QStringList m_recentCommands;
    QStringList m_recentSearches; // window feeds settings recents for empty input
    QString m_currentQuery;
    // Parsed query (field filters, free text, problems) behind the last search.
    SearchEngine::ParsedQuery m_parsed;
    // Ghost completion preview (U9): dim suffix shown after the caret.
    QLabel *m_ghost = nullptr;
    // Empty-input recents section (U9): recent searches + recent commands.
    struct RecentRow { QString kind; QString text; QString payload; };
    QVector<RecentRow> m_recentRows;

public:
    // Test seams (U9): candidate ghost text + recents section without widgets.
    static QString ghostSuffix(const QString &input, const QString &candidate);
    void setRecentSearches(const QStringList &searches) { m_recentSearches = searches; }
    QVector<RecentRow> recentRows() const { return m_recentRows; }
    void rebuildRecentRows();
};
