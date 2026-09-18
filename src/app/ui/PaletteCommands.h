#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * @brief The command set behind the palette's '>' prefix.
 *
 * Parsing, alias resolution, suggestions and argument completion are pure
 * functions over strings, so the palette's behaviour is unit-testable without
 * a widget or a database.
 */
namespace PaletteCommands {

// What a command expects after its name.
enum class Argument {
    None,   // completes immediately (acts on the main window's entry)
    Tag,    // completion from the tags that exist
    Group,  // completion from the group tree
    Format, // completion from the export formats
};

struct Command {
    QString id;          // canonical id, also the value stored in history
    QStringList aliases; // what the user may type (>d, >del, >delete)
    QString usage;       // shown in the command list
    QString description;
    Argument argument = Argument::None;

    bool takesArgument() const { return argument != Argument::None; }
};

QVector<Command> all();

// Case-insensitive: exact alias first, then a unique prefix of any alias
// (so ">del" resolves, while an ambiguous prefix does not).
const Command *find(const QString &word);

struct Parsed {
    bool hasPrefix = false;   // the input started with '>'
    QString word;             // the typed command word, lowercased
    QString argument;         // everything after the first space
    const Command *command = nullptr; // null when the word is empty or unknown
    bool ambiguous = false;   // unknown word, but other commands start with it
};

// Splits ">tag work" into {tag, "work"}. Free text (no '>') stays untouched.
Parsed parse(const QString &input);

// Commands whose id or an alias starts with `word`; every command for an empty
// word, so a bare ">" lists them all.
QVector<Command> suggest(const QString &word);

// The subset of `candidates` matching `argument`: entries that start with it
// come first (in candidate order), substring matches after, duplicates removed.
// An empty argument matches everything, so a bare ">tag " lists all tags.
QStringList completions(Argument kind, const QStringList &candidates, const QString &argument);

// Candidate values for argument completion that don't come from the database.
QStringList staticCandidates(Argument kind);

} // namespace PaletteCommands
