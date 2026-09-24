#include <QtTest>

#include "PaletteCommands.h"

// The palette's '>' language is pure string logic: parsing, alias resolution
// and completion are tested here without a widget or a database.
class TestPalette : public QObject
{
    Q_OBJECT

private slots:
    void parsesCommandsAndArguments();
    void resolvesAliasesAndUniquePrefixes();
    void refusesAmbiguousPrefixes();
    void treatsPlainTextAsSearch();
    void suggestsCommandsForPartialWords();
    void completesPrefixBeforeSubstring();
    void offersStaticFormatCandidates();
    void profileCommandParsesResolvesSuggestsCompletes();

private:
    static QStringList ids(const QVector<PaletteCommands::Command> &commands);
};

QStringList TestPalette::ids(const QVector<PaletteCommands::Command> &commands)
{
    QStringList out;
    for (const PaletteCommands::Command &command : commands)
        out << command.id;
    return out;
}

void TestPalette::parsesCommandsAndArguments()
{
    const PaletteCommands::Parsed bare = PaletteCommands::parse(QStringLiteral(">tag"));
    QVERIFY(bare.hasPrefix);
    QCOMPARE(bare.word, QStringLiteral("tag"));
    QVERIFY(bare.argument.isEmpty());
    QVERIFY(bare.command != nullptr);
    QCOMPARE(bare.command->id, QStringLiteral("tag"));
    QCOMPARE(bare.command->argument, PaletteCommands::Argument::Tag);

    const PaletteCommands::Parsed withArgument =
        PaletteCommands::parse(QStringLiteral("  >export  markdown  "));
    QVERIFY(withArgument.hasPrefix);
    QCOMPARE(withArgument.word, QStringLiteral("export"));
    QCOMPARE(withArgument.argument, QStringLiteral("markdown"));

    // Extra spaces inside an argument are preserved (a tag can contain them).
    const PaletteCommands::Parsed spaced = PaletteCommands::parse(QStringLiteral(">tag two words"));
    QCOMPARE(spaced.argument, QStringLiteral("two words"));

    // Command words are case-insensitive; the '>' prefix is required.
    const PaletteCommands::Parsed upper = PaletteCommands::parse(QStringLiteral(">DELETE"));
    QVERIFY(upper.command != nullptr);
    QCOMPARE(upper.command->id, QStringLiteral("delete"));
    QVERIFY(!PaletteCommands::parse(QStringLiteral("delete")).hasPrefix);
}

void TestPalette::resolvesAliasesAndUniquePrefixes()
{
    QCOMPARE(PaletteCommands::find(QStringLiteral("del"))->id, QStringLiteral("delete"));
    QCOMPARE(PaletteCommands::find(QStringLiteral("dele"))->id, QStringLiteral("delete"));
    QCOMPARE(PaletteCommands::find(QStringLiteral("cl"))->id, QStringLiteral("clean"));
    QCOMPARE(PaletteCommands::find(QStringLiteral("conf"))->id, QStringLiteral("settings"));
    QCOMPARE(PaletteCommands::find(QStringLiteral("m"))->id, QStringLiteral("group"));
    QVERIFY(PaletteCommands::find(QString()) == nullptr);
    QVERIFY(PaletteCommands::find(QStringLiteral("nonsense")) == nullptr);
}

void TestPalette::refusesAmbiguousPrefixes()
{
    // The pre-existing one-letter aliases keep working...
    const PaletteCommands::Command *copy = PaletteCommands::find(QStringLiteral("c"));
    QVERIFY(copy != nullptr);
    QCOMPARE(copy->id, QStringLiteral("copy"));
    const PaletteCommands::Command *pin = PaletteCommands::find(QStringLiteral("p"));
    QVERIFY(pin != nullptr);
    QCOMPARE(pin->id, QStringLiteral("pin"));
    const PaletteCommands::Command *transform = PaletteCommands::find(QStringLiteral("t"));
    QVERIFY(transform != nullptr);
    QCOMPARE(transform->id, QStringLiteral("transform"));

    // ...but a prefix that hits several commands must not silently pick one
    // ("co" is copy and config, "s" would be snippet and settings).
    QVERIFY(PaletteCommands::find(QStringLiteral("co")) == nullptr);
    const PaletteCommands::Parsed ambiguous = PaletteCommands::parse(QStringLiteral(">co"));
    QVERIFY(ambiguous.command == nullptr);
    QVERIFY(ambiguous.ambiguous); // ...but the palette can offer both
    QCOMPARE(ids(PaletteCommands::suggest(QStringLiteral("co"))),
             (QStringList{QStringLiteral("copy"), QStringLiteral("settings")}));
}

void TestPalette::treatsPlainTextAsSearch()
{
    const PaletteCommands::Parsed plain = PaletteCommands::parse(QStringLiteral("git commit"));
    QVERIFY(!plain.hasPrefix);
    QVERIFY(plain.command == nullptr);
    QVERIFY(plain.word.isEmpty());
    QVERIFY(plain.argument.isEmpty());

    // A lone '>' is a valid prefix with an empty word: the palette lists them all.
    const PaletteCommands::Parsed empty = PaletteCommands::parse(QStringLiteral(">"));
    QVERIFY(empty.hasPrefix);
    QVERIFY(empty.word.isEmpty());
    QVERIFY(empty.command == nullptr);
    QVERIFY(!empty.ambiguous); // not an error, just "show everything"
}

void TestPalette::suggestsCommandsForPartialWords()
{
    QCOMPARE(ids(PaletteCommands::suggest(QStringLiteral("de"))), QStringList{QStringLiteral("delete")});
    QCOMPARE(ids(PaletteCommands::suggest(QStringLiteral("clean")))
                 .value(0), QStringLiteral("clean"));
    // Every command is offered for an empty word (the bare ">" case).
    QCOMPARE(PaletteCommands::suggest(QString()).size(), PaletteCommands::all().size());
    QVERIFY(PaletteCommands::suggest(QStringLiteral("zzz")).isEmpty());
}

void TestPalette::completesPrefixBeforeSubstring()
{
    const QStringList candidates = {QStringLiteral("work"), QStringLiteral("work-in-progress"),
                                    QStringLiteral("network"), QStringLiteral("workshop")};
    // Prefix matches first, in candidate order, then substring matches.
    QCOMPARE(PaletteCommands::completions(PaletteCommands::Argument::Tag, candidates,
                                          QStringLiteral("work")),
             (QStringList{QStringLiteral("work"), QStringLiteral("work-in-progress"),
                          QStringLiteral("workshop"), QStringLiteral("network")}));
    // Case-insensitive, and an empty argument lists everything.
    QCOMPARE(PaletteCommands::completions(PaletteCommands::Argument::Group, candidates,
                                          QStringLiteral("WO")),
             (QStringList{QStringLiteral("work"), QStringLiteral("work-in-progress"),
                          QStringLiteral("workshop"), QStringLiteral("network")}));
    QCOMPARE(PaletteCommands::completions(PaletteCommands::Argument::Tag, candidates, QString()).size(),
             4);
    // Duplicates collapse and unmatched arguments leave the list empty.
    QCOMPARE(PaletteCommands::completions(PaletteCommands::Argument::Tag,
                                          {QStringLiteral("a"), QStringLiteral("a")}, QStringLiteral("a")),
             QStringList{QStringLiteral("a")});
    QVERIFY(PaletteCommands::completions(PaletteCommands::Argument::Tag, candidates,
                                         QStringLiteral("zzz")).isEmpty());
}

void TestPalette::offersStaticFormatCandidates()
{
    QCOMPARE(PaletteCommands::staticCandidates(PaletteCommands::Argument::Format),
             (QStringList{QStringLiteral("json"), QStringLiteral("markdown"), QStringLiteral("csv"),
                          QStringLiteral("html"), QStringLiteral("images")}));
    QVERIFY(PaletteCommands::staticCandidates(PaletteCommands::Argument::Tag).isEmpty());
    QVERIFY(PaletteCommands::staticCandidates(PaletteCommands::Argument::Group).isEmpty());
    QVERIFY(PaletteCommands::staticCandidates(PaletteCommands::Argument::None).isEmpty());

    // Formats complete like any other argument.
    QCOMPARE(PaletteCommands::completions(PaletteCommands::Argument::Format,
                                          PaletteCommands::staticCandidates(
                                              PaletteCommands::Argument::Format),
                                          QStringLiteral("mar")),
             QStringList{QStringLiteral("markdown")});
}

void TestPalette::profileCommandParsesResolvesSuggestsCompletes()
{
    // U14 profiles: `>profile <name>` switches the whole setting set, with
    // profile-name completion from the saved profiles.
    const PaletteCommands::Parsed parsed =
        PaletteCommands::parse(QStringLiteral(">profile Work"));
    QVERIFY(parsed.hasPrefix);
    QCOMPARE(parsed.word, QStringLiteral("profile"));
    QCOMPARE(parsed.argument, QStringLiteral("Work"));
    QVERIFY(parsed.command != nullptr);
    QCOMPARE(parsed.command->id, QStringLiteral("profile"));
    QCOMPARE(parsed.command->argument, PaletteCommands::Argument::Profile);

    // The short alias and unique prefixes resolve; "pr" stays ambiguous with
    // settings' "prefs" alias instead of silently picking one.
    QCOMPARE(PaletteCommands::find(QStringLiteral("prof"))->id, QStringLiteral("profile"));
    QCOMPARE(PaletteCommands::find(QStringLiteral("pro"))->id, QStringLiteral("profile"));
    QVERIFY(PaletteCommands::find(QStringLiteral("pr")) == nullptr);
    const QStringList prIds = ids(PaletteCommands::suggest(QStringLiteral("pr")));
    QVERIFY(prIds.contains(QStringLiteral("profile")));
    QVERIFY(prIds.contains(QStringLiteral("settings")));

    // Saved profile names complete like tags/groups (prefix first, then
    // substring, case-insensitive).
    const QStringList names = {QStringLiteral("Work"), QStringLiteral("Personal"),
                               QStringLiteral("network-test")};
    QCOMPARE(PaletteCommands::completions(PaletteCommands::Argument::Profile, names,
                                          QStringLiteral("per")),
             (QStringList{QStringLiteral("Personal")}));
    QCOMPARE(PaletteCommands::completions(PaletteCommands::Argument::Profile, names,
                                          QStringLiteral("WORK")),
             (QStringList{QStringLiteral("Work"), QStringLiteral("network-test")}));
    QVERIFY(PaletteCommands::staticCandidates(PaletteCommands::Argument::Profile).isEmpty());
}

QTEST_MAIN(TestPalette)
#include "tst_palette.moc"
