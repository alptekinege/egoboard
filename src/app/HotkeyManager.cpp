#include "HotkeyManager.h"

#include <KActionCollection>
#include <KGlobalAccel>

#include <QAction>

#include <algorithm>

QList<QKeySequence> HotkeyManager::defaultToggleShortcut()
{
    return {QKeySequence(QStringLiteral("Meta+Shift+V"))};
}

QList<QKeySequence> HotkeyManager::defaultQuickPasteShortcut()
{
    return {QKeySequence(QStringLiteral("Meta+V"))};
}

QList<QKeySequence> HotkeyManager::defaultDeleteLastShortcut()
{
    return {QKeySequence(QStringLiteral("Meta+Shift+D"))};
}

QList<QKeySequence> HotkeyManager::defaultPauseShortcut()
{
    return {QKeySequence(QStringLiteral("Meta+Shift+P"))};
}

QList<QKeySequence> HotkeyManager::reservedSequences()
{
    QList<QKeySequence> reserved;
    const QList<QList<QKeySequence>> groups = {defaultToggleShortcut(), defaultQuickPasteShortcut(),
                                               defaultDeleteLastShortcut(), defaultPauseShortcut()};
    for (const QList<QKeySequence> &group : groups) {
        for (const QKeySequence &sequence : group) {
            if (!sequence.isEmpty())
                reserved.append(sequence);
        }
    }
    return reserved;
}

QVector<HotkeyManager::SnippetBinding>
HotkeyManager::resolveSnippetShortcuts(const QVector<Snippet> &snippets,
                                       const QList<QKeySequence> &reserved)
{
    QVector<Snippet> ordered = snippets;
    std::sort(ordered.begin(), ordered.end(),
              [](const Snippet &a, const Snippet &b) { return a.id < b.id; });

    QHash<QString, QString> takenBy; // portable sequence → snippet name
    QHash<QString, QString> reservedText;
    for (const QKeySequence &sequence : reserved) {
        if (!sequence.isEmpty())
            reservedText.insert(sequence.toString(QKeySequence::PortableText), QString());
    }

    QVector<SnippetBinding> bindings;
    for (const Snippet &snippet : ordered) {
        const QString stored = snippet.shortcut.trimmed();
        if (stored.isEmpty())
            continue; // shortcut is optional

        SnippetBinding binding;
        binding.id = snippet.id;
        binding.name = snippet.name;
        binding.shortcut = stored;
        binding.sequence = QKeySequence::fromString(stored, QKeySequence::PortableText);
        const QString portable = binding.sequence.toString(QKeySequence::PortableText);
        // Unparsable text still yields a stub sequence holding Qt::Key_unknown,
        // whose portable text is empty — that is what KGlobalAccel needs.
        if (portable.isEmpty()) {
            binding.sequence = QKeySequence();
            binding.problem = tr("\"%1\": \"%2\" is not a shortcut Egoboard can bind.")
                                  .arg(snippet.name, stored);
        } else if (reservedText.contains(portable)) {
            binding.problem = tr("\"%1\": %2 is reserved for Egoboard itself.").arg(snippet.name, portable);
        } else if (takenBy.contains(portable)) {
            binding.problem = tr("\"%1\": %2 is already used by \"%3\".")
                                  .arg(snippet.name, portable, takenBy.value(portable));
        } else {
            takenBy.insert(portable, snippet.name);
        }
        bindings.append(binding);
    }
    return bindings;
}

QStringList HotkeyManager::setSnippetShortcuts(const QVector<Snippet> &snippets)
{
    const QVector<SnippetBinding> bindings = resolveSnippetShortcuts(snippets, reservedSequences());

    QSet<qint64> live;
    QStringList problems;
    for (const SnippetBinding &binding : bindings) {
        if (!binding.problem.isEmpty()) {
            problems.append(binding.problem);
            continue;
        }
        live.insert(binding.id);
        QAction *action = m_snippetActions.value(binding.id, nullptr);
        if (!action) {
            action = m_collection->addAction(QStringLiteral("snippet-%1").arg(binding.id));
            connect(action, &QAction::triggered, this, [this, id = binding.id] {
                emit snippetRequested(id);
            });
            m_snippetActions.insert(binding.id, action);
        }
        action->setText(tr("Paste snippet: %1").arg(binding.name));
        const QString portable = binding.sequence.toString(QKeySequence::PortableText);
        if (m_appliedSnippetSequences.value(binding.id) != portable) {
            // The database is the source of truth here, so the stored sequence
            // must win over whatever KGlobalAccel saved for this action: the
            // value passed with the default (Autoloading) flag is honoured only
            // the first time an action name is ever registered.
            KGlobalAccel::self()->setDefaultShortcut(action, {binding.sequence});
            KGlobalAccel::self()->setShortcut(action, {binding.sequence}, KGlobalAccel::NoAutoloading);
            m_appliedSnippetSequences.insert(binding.id, portable);
        }
    }

    // Snippets that lost their shortcut (or were deleted) drop their action.
    const QList<qint64> known = m_snippetActions.keys();
    for (qint64 id : known) {
        if (live.contains(id))
            continue;
        QAction *action = m_snippetActions.take(id);
        m_appliedSnippetSequences.remove(id);
        // Assign "nothing" before dropping the action, again with NoAutoloading
        // so KGlobalAccel does not fall back to the saved sequence: an action
        // whose snippet is gone must not keep its key grabbed until exit.
        KGlobalAccel::self()->setShortcut(action, {}, KGlobalAccel::NoAutoloading);
        m_collection->removeAction(action); // owns the action and deletes it
    }
    return problems;
}

void HotkeyManager::setPaused(bool paused)
{
    m_pause->setChecked(paused); // setChecked does not emit triggered()
}

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    auto collection = new KActionCollection(this);
    collection->setComponentName(QStringLiteral("egoboard"));
    m_collection = collection;

    m_toggle = collection->addAction(QStringLiteral("toggle"));
    m_toggle->setText(tr("Show Egoboard Clipboard History"));
    KGlobalAccel::self()->setDefaultShortcut(m_toggle, defaultToggleShortcut());
    KGlobalAccel::self()->setShortcut(m_toggle, defaultToggleShortcut());
    connect(m_toggle, &QAction::triggered, this, &HotkeyManager::toggleRequested);

    m_quickPaste = collection->addAction(QStringLiteral("quickpaste"));
    m_quickPaste->setText(tr("Open Quick Paste Menu"));
    KGlobalAccel::self()->setDefaultShortcut(m_quickPaste, defaultQuickPasteShortcut());
    KGlobalAccel::self()->setShortcut(m_quickPaste, defaultQuickPasteShortcut());
    connect(m_quickPaste, &QAction::triggered, this, &HotkeyManager::quickPasteRequested);

    m_deleteLast = collection->addAction(QStringLiteral("deletelast"));
    m_deleteLast->setText(tr("Delete Last Clipboard Entry"));
    KGlobalAccel::self()->setDefaultShortcut(m_deleteLast, defaultDeleteLastShortcut());
    KGlobalAccel::self()->setShortcut(m_deleteLast, defaultDeleteLastShortcut());
    connect(m_deleteLast, &QAction::triggered, this, &HotkeyManager::deleteLastRequested);

    m_pause = collection->addAction(QStringLiteral("pausecapture"));
    m_pause->setText(tr("Pause/Resume Clipboard Capture"));
    m_pause->setCheckable(true);
    KGlobalAccel::self()->setDefaultShortcut(m_pause, defaultPauseShortcut());
    KGlobalAccel::self()->setShortcut(m_pause, defaultPauseShortcut());
    connect(m_pause, &QAction::triggered, this,
            [this](bool checked) { emit pauseToggleRequested(checked); });
}
