#include "PaletteCommands.h"

#include <QSet>

namespace PaletteCommands {

const QVector<Command> &table()
{
    static const QVector<Command> commands = {
        // The one-letter aliases are the ones the palette had before the
        // command table existed; they keep working.
        {QStringLiteral("transform"),
         {QStringLiteral("transform"), QStringLiteral("xform"), QStringLiteral("tr"),
          QStringLiteral("t")},
         QStringLiteral(">transform [filter]"),
         QStringLiteral("Apply a transform (built-in or JS script) to the selected entry")},
        {QStringLiteral("snippet"),
         {QStringLiteral("snippet"), QStringLiteral("snip"), QStringLiteral("s")},
         QStringLiteral(">snippet [filter]"),
         QStringLiteral("Expand a snippet with the selected entry or the clipboard")},
        {QStringLiteral("pin"), {QStringLiteral("pin"), QStringLiteral("p")},
         QStringLiteral(">pin"), QStringLiteral("Pin or unpin the selected entry")},
        {QStringLiteral("copy"), {QStringLiteral("copy"), QStringLiteral("c")},
         QStringLiteral(">copy"), QStringLiteral("Copy the selected entry to the clipboard")},
        {QStringLiteral("delete"),
         {QStringLiteral("delete"), QStringLiteral("del"), QStringLiteral("d")},
         QStringLiteral(">delete"), QStringLiteral("Delete the selected entry")},
        {QStringLiteral("tag"), {QStringLiteral("tag")}, QStringLiteral(">tag <name>"),
         QStringLiteral("Tag the selected entry (new tags are created)"), Argument::Tag},
        {QStringLiteral("group"),
         {QStringLiteral("group"), QStringLiteral("move"), QStringLiteral("g")},
         QStringLiteral(">group <name>"),
         QStringLiteral("Move the selected entry into a group (created if unknown)"),
         Argument::Group},
        {QStringLiteral("export"), {QStringLiteral("export"), QStringLiteral("ex")},
         QStringLiteral(">export [json|markdown|csv|html|images]"),
         QStringLiteral("Export the history; without a format the export dialog opens"),
         Argument::Format},
        {QStringLiteral("pause"), {QStringLiteral("pause"), QStringLiteral("resume")},
         QStringLiteral(">pause"), QStringLiteral("Pause or resume clipboard capture")},
        {QStringLiteral("settings"),
         {QStringLiteral("settings"), QStringLiteral("config"), QStringLiteral("prefs")},
         QStringLiteral(">settings"), QStringLiteral("Open the settings dialog")},
        {QStringLiteral("tour"), {QStringLiteral("tour")},
         QStringLiteral(">tour"), QStringLiteral("Show the first-run introduction tour")},
        {QStringLiteral("dashboard"),
         {QStringLiteral("dashboard"), QStringLiteral("stats"), QStringLiteral("usage")},
         QStringLiteral(">dashboard"), QStringLiteral("Show the local usage dashboard")},
        {QStringLiteral("clean"), {QStringLiteral("clean"), QStringLiteral("clear")},
         QStringLiteral(">clean"), QStringLiteral("Delete the clipboard history (asks first)")},
        {QStringLiteral("profile"), {QStringLiteral("profile"), QStringLiteral("prof")},
         QStringLiteral(">profile <name>"),
         QStringLiteral("Switch settings profile (saved setting sets)"), Argument::Profile},
    };
    return commands;
}

QVector<Command> all()
{
    return table();
}

const Command *find(const QString &word)
{
    if (word.isEmpty())
        return nullptr;
    const QString needle = word.toLower();

    for (const Command &command : table()) {
        for (const QString &alias : command.aliases) {
            if (alias == needle)
                return &command; // exact alias always wins
        }
    }
    const Command *match = nullptr;
    for (const Command &command : table()) {
        for (const QString &alias : command.aliases) {
            if (!alias.startsWith(needle))
                continue;
            if (match && match != &command)
                return nullptr; // ambiguous: the caller offers suggestions instead
            match = &command;
        }
    }
    return match;
}

QVector<Command> suggest(const QString &word)
{
    const QString needle = word.toLower();
    QVector<Command> out;
    for (const Command &command : table()) {
        bool hit = needle.isEmpty();
        for (const QString &alias : command.aliases) {
            if (alias.startsWith(needle)) {
                hit = true;
                break;
            }
        }
        if (hit)
            out.append(command);
    }
    return out;
}

Parsed parse(const QString &input)
{
    Parsed parsed;
    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith(QLatin1Char('>')))
        return parsed; // free text: history search
    parsed.hasPrefix = true;

    const QString after = trimmed.mid(1).trimmed();
    const int space = after.indexOf(QLatin1Char(' '));
    if (space < 0) {
        parsed.word = after;
    } else {
        parsed.word = after.left(space);
        parsed.argument = after.mid(space + 1).trimmed();
    }
    parsed.command = find(parsed.word);
    // Unresolved but recognizable ("de" → delete): show the matching commands
    // instead of an empty result list. A bare ">" is not ambiguous — it asks for
    // the recent/all command list.
    parsed.ambiguous = parsed.command == nullptr && !parsed.word.isEmpty()
        && !suggest(parsed.word).isEmpty();
    return parsed;
}

QStringList completions(Argument kind, const QStringList &candidates, const QString &argument)
{
    Q_UNUSED(kind)
    const QString needle = argument.trimmed().toLower();
    QStringList prefixMatches;
    QStringList substringMatches;
    QSet<QString> seen;
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty() || seen.contains(candidate))
            continue;
        seen.insert(candidate);
        const QString lower = candidate.toLower();
        if (needle.isEmpty() || lower.startsWith(needle))
            prefixMatches.append(candidate);
        else if (lower.contains(needle))
            substringMatches.append(candidate);
    }
    return prefixMatches + substringMatches;
}

QStringList staticCandidates(Argument kind)
{
    switch (kind) {
    case Argument::Format:
        return {QStringLiteral("json"), QStringLiteral("markdown"), QStringLiteral("csv"),
                QStringLiteral("html"), QStringLiteral("images")};
    case Argument::None:
    case Argument::Tag:
    case Argument::Group:
    case Argument::Profile:
        break;
    }
    return {};
}

} // namespace PaletteCommands
