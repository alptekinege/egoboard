#pragma once

#include <QString>
#include <QVector>

/**
 * @brief The per-match actions offered by the KRunner plugin.
 *
 * The ids travel through KRunner and come back in `run()`, and each one maps to
 * a method of the D-Bus adaptor. Both sides live in different libraries, so the
 * protocol (ids, method names, the pin flag) is defined once here and can be
 * tested without KRunner itself.
 */
namespace RunnerActions {

enum class Kind {
    Paste,
    Copy,
    Pin,
    Unpin,
    Delete,
};

// "egoboard-paste", "egoboard-copy", … — what KRunner hands back.
QString id(Kind kind);

// The D-Bus method on org.egoboard.Egoboard that performs the action.
QString dbusMethod(Kind kind);

// Value for the boolean argument of Pin(); meaningless for the other kinds.
bool pinnedFlag(Kind kind);

// Unknown or empty ids fall back to Paste, so a future KRunner version that
// sends something unexpected still does the obvious thing.
Kind kindForId(const QString &actionId);

// Paste first, then the management actions, in the order the menu shows them.
QVector<Kind> all();

} // namespace RunnerActions
