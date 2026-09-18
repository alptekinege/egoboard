#include <QtTest>

#include "HotkeyManager.h"
#include "SnippetManager.h"

// Snippet shortcuts come from the database, so resolution (parsing, duplicates,
// reserved sequences) is pure logic and testable without KGlobalAccel.
class TestHotkeys : public QObject
{
    Q_OBJECT

private slots:
    void bindsValidSnippetShortcuts();
    void skipsSnippetsWithoutShortcut();
    void rejectsUnparsableShortcut();
    void keepsFirstOfDuplicateShortcuts();
    void rejectsReservedSequences();
    void reservedSequencesCoverTheAppHotkeys();

private:
    static Snippet snippet(qint64 id, const QString &name, const QString &shortcut);
    static QStringList problems(const QVector<HotkeyManager::SnippetBinding> &bindings);
};

Snippet TestHotkeys::snippet(qint64 id, const QString &name, const QString &shortcut)
{
    Snippet s;
    s.id = id;
    s.name = name;
    s.templateText = QStringLiteral("template");
    s.shortcut = shortcut;
    return s;
}

QStringList TestHotkeys::problems(const QVector<HotkeyManager::SnippetBinding> &bindings)
{
    QStringList out;
    for (const auto &binding : bindings) {
        if (!binding.problem.isEmpty())
            out << binding.problem;
    }
    return out;
}

void TestHotkeys::bindsValidSnippetShortcuts()
{
    const QVector<Snippet> snippets = {
        snippet(1, QStringLiteral("git commit"), QStringLiteral("Meta+Shift+G")),
        snippet(2, QStringLiteral("signature"), QStringLiteral("Ctrl+Alt+S")),
    };

    const auto bindings = HotkeyManager::resolveSnippetShortcuts(snippets, {});
    QCOMPARE(bindings.size(), 2);
    QCOMPARE(bindings.at(0).id, qint64(1));
    QCOMPARE(bindings.at(0).sequence, QKeySequence(QStringLiteral("Meta+Shift+G")));
    QCOMPARE(bindings.at(0).shortcut, QStringLiteral("Meta+Shift+G"));
    QCOMPARE(bindings.at(0).name, QStringLiteral("git commit"));
    QVERIFY(bindings.at(0).problem.isEmpty());
    QCOMPARE(bindings.at(1).sequence, QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    QVERIFY(problems(bindings).isEmpty());

    // Stored text is trusted loosely: surrounding whitespace is trimmed.
    const auto padded = HotkeyManager::resolveSnippetShortcuts(
        {snippet(3, QStringLiteral("padded"), QStringLiteral("  Meta+G  "))}, {});
    QCOMPARE(padded.size(), 1);
    QCOMPARE(padded.at(0).sequence, QKeySequence(QStringLiteral("Meta+G")));
    QVERIFY(padded.at(0).problem.isEmpty());
}

void TestHotkeys::skipsSnippetsWithoutShortcut()
{
    const QVector<Snippet> snippets = {
        snippet(1, QStringLiteral("unbound"), QString()),
        snippet(2, QStringLiteral("blank"), QStringLiteral("   ")),
        snippet(3, QStringLiteral("bound"), QStringLiteral("Meta+B")),
    };

    const auto bindings = HotkeyManager::resolveSnippetShortcuts(snippets, {});
    QCOMPARE(bindings.size(), 1); // optional shortcuts are not problems
    QCOMPARE(bindings.at(0).id, qint64(3));
    QVERIFY(problems(bindings).isEmpty());
}

void TestHotkeys::rejectsUnparsableShortcut()
{
    const auto bindings = HotkeyManager::resolveSnippetShortcuts(
        {snippet(1, QStringLiteral("broken"), QStringLiteral("not a key")),
         snippet(2, QStringLiteral("modifier only"), QStringLiteral("Meta+"))},
        {});
    QCOMPARE(bindings.size(), 2);
    // QKeySequence::fromString returns a Key_unknown stub for junk text, so the
    // usable/rejected distinction is the parsed portable text, not isEmpty().
    QVERIFY(bindings.at(0).sequence.isEmpty());
    QVERIFY(!bindings.at(0).problem.isEmpty());
    QVERIFY(bindings.at(0).problem.contains(QStringLiteral("not a key")));
    QVERIFY(bindings.at(1).sequence.isEmpty());
    QVERIFY(!bindings.at(1).problem.isEmpty());

    // A full-width stroke like a plain letter is fine (no modifier required).
    const auto letter = HotkeyManager::resolveSnippetShortcuts(
        {snippet(3, QStringLiteral("letter"), QStringLiteral("g"))}, {});
    QCOMPARE(letter.size(), 1);
    QVERIFY(letter.at(0).problem.isEmpty());
    QCOMPARE(letter.at(0).sequence.toString(QKeySequence::PortableText), QStringLiteral("G"));
}

void TestHotkeys::keepsFirstOfDuplicateShortcuts()
{
    // Order in the vector must not matter: the lowest id owns the sequence.
    const QVector<Snippet> snippets = {
        snippet(7, QStringLiteral("second"), QStringLiteral("Meta+Shift+G")),
        snippet(4, QStringLiteral("first"), QStringLiteral("Meta+Shift+G")),
    };

    const auto bindings = HotkeyManager::resolveSnippetShortcuts(snippets, {});
    QCOMPARE(bindings.size(), 2);
    QCOMPARE(bindings.at(0).id, qint64(4)); // sorted by id, not by input order
    QVERIFY(bindings.at(0).problem.isEmpty());
    QCOMPARE(bindings.at(1).id, qint64(7));
    QVERIFY(bindings.at(1).problem.contains(QStringLiteral("first")));

    // Equivalent spellings of the same sequence still collide.
    const auto spelled = HotkeyManager::resolveSnippetShortcuts(
        {snippet(1, QStringLiteral("a"), QStringLiteral("Ctrl+Shift+G")),
         snippet(2, QStringLiteral("b"), QStringLiteral("Shift+Ctrl+G"))},
        {});
    QCOMPARE(spelled.size(), 2);
    QVERIFY(spelled.at(0).problem.isEmpty());
    QVERIFY(!spelled.at(1).problem.isEmpty());
}

void TestHotkeys::rejectsReservedSequences()
{
    const QList<QKeySequence> reserved = HotkeyManager::reservedSequences();
    const QVector<Snippet> snippets = {
        snippet(1, QStringLiteral("hijack"), QStringLiteral("Meta+V")), // quick paste
        snippet(2, QStringLiteral("fine"), QStringLiteral("Meta+Shift+G")),
    };

    const auto bindings = HotkeyManager::resolveSnippetShortcuts(snippets, reserved);
    QCOMPARE(bindings.size(), 2);
    QVERIFY(bindings.at(0).problem.contains(QStringLiteral("reserved")));
    QVERIFY(bindings.at(1).problem.isEmpty());

    // Without the reserved list (pure resolution) the same sequence is fine,
    // which is what makes the check a separate concern from parsing.
    const auto unreserved = HotkeyManager::resolveSnippetShortcuts(snippets, {});
    QVERIFY(problems(unreserved).isEmpty());
}

void TestHotkeys::reservedSequencesCoverTheAppHotkeys()
{
    const QList<QKeySequence> reserved = HotkeyManager::reservedSequences();
    const QList<QList<QKeySequence>> defaults = {
        HotkeyManager::defaultToggleShortcut(), HotkeyManager::defaultQuickPasteShortcut(),
        HotkeyManager::defaultDeleteLastShortcut(), HotkeyManager::defaultPauseShortcut()};
    for (const QList<QKeySequence> &group : defaults) {
        for (const QKeySequence &sequence : group)
            QVERIFY(reserved.contains(sequence));
    }
    // The pause shortcut is one of the four the snippets must keep away from.
    QVERIFY(HotkeyManager::defaultPauseShortcut().contains(QKeySequence(QStringLiteral("Meta+Shift+P"))));
}

QTEST_MAIN(TestHotkeys)
#include "tst_hotkeys.moc"
