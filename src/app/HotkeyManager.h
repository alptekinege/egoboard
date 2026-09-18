#pragma once

#include "SnippetManager.h"

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QVector>

class KActionCollection;
class QAction;

// Registers the global shortcuts through KGlobalAccel (so they work system
// wide, are configurable in Plasma's shortcut editor and survive on Wayland).
//   toggle     - show/hide the history window   (default Meta+Shift+V)
//   quickpaste - quick paste popup               (default Meta+V)
//   deletelast - drop the newest capture          (default Meta+Shift+D)
//   pause      - pause/resume clipboard capture   (default Meta+Shift+P)
// Plus one action per snippet that has a shortcut in the database.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject *parent = nullptr);

    QAction *toggleAction() const { return m_toggle; }
    QAction *quickPasteAction() const { return m_quickPaste; }
    QAction *deleteLastAction() const { return m_deleteLast; }
    QAction *pauseAction() const { return m_pause; }

    static QList<QKeySequence> defaultToggleShortcut();
    static QList<QKeySequence> defaultQuickPasteShortcut();
    static QList<QKeySequence> defaultDeleteLastShortcut();
    static QList<QKeySequence> defaultPauseShortcut();

    // Sequences Egoboard keeps for the four shortcuts above; snippet hotkeys
    // refuse to take them.
    static QList<QKeySequence> reservedSequences();

    // One snippet shortcut as stored, with the outcome of parsing it. A usable
    // binding has a non-empty `sequence` and an empty `problem`; rejected ones
    // keep the stored text so the UI can quote it.
    struct SnippetBinding {
        qint64 id = 0;
        QString name;
        QString shortcut;      // stored text, trimmed
        QKeySequence sequence; // parsed; empty when the shortcut is unusable
        QString problem;       // user-facing reason, empty when usable
    };

    // Resolves stored snippet shortcuts into bindable key sequences: entries
    // without a shortcut are left out, unreachable/invalid ones and the
    // reserved sequences are rejected, and of two snippets sharing a sequence
    // the one with the lower id wins. Pure, so it is unit-testable.
    static QVector<SnippetBinding> resolveSnippetShortcuts(const QVector<Snippet> &snippets,
                                                           const QList<QKeySequence> &reserved);

    // Binds every usable snippet shortcut, unbinds shortcuts of deleted ones
    // and returns one line per snippet whose shortcut could not be used. Safe to
    // call repeatedly (unchanged sequences are not re-applied, so a rebinding
    // done in Plasma's editor is preserved until the snippet itself changes).
    QStringList setSnippetShortcuts(const QVector<Snippet> &snippets);

    // Keeps the checkable pause action in sync with the real capture state
    // (e.g. when the tray menu toggled it or the session locked).
    void setPaused(bool paused);

signals:
    void toggleRequested();
    void quickPasteRequested();
    void deleteLastRequested();
    void pauseToggleRequested(bool paused);
    void snippetRequested(qint64 snippetId);

private:
    KActionCollection *m_collection = nullptr;
    QAction *m_toggle = nullptr;
    QAction *m_quickPaste = nullptr;
    QAction *m_deleteLast = nullptr;
    QAction *m_pause = nullptr;
    QHash<qint64, QAction *> m_snippetActions;
    QHash<qint64, QString> m_appliedSnippetSequences;
};
