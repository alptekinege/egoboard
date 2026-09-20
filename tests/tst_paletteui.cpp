#include <QtTest>

#include "ClipboardRecord.h"
#include "CommandPalette.h"
#include "ScriptActionManager.h"
#include "SnippetManager.h"
#include "StorageManager.h"

#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

// Drives the real palette widget: the command table itself is covered by
// tst_palette, this checks the wiring around it (mode switching, argument
// completion, the signals the main window acts on).
class TestPaletteUi : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void listsCommandsForBarePrefix();
    void narrowsCommandsWhileTyping();
    void completesTagsAndRunsTheCommand();
    void completesTagsByTabWithoutRunning();
    void freeTextTagStillRuns();
    void argumentlessCommandEmitsForTheSelection();
    void completesExportFormats();
    void plainTextStillSearchesHistory();
    void noArgumentCommandWithEmptyInputDoesNothing();
    void unknownCommandShowsSuggestions();
    void emptyInputShowsRecentSearchesAndCommands();
    void ghostSuffixIsTheCompletableRemainder();
    void historyRowsCarryTypeAndSource();

private:
    QModelIndex firstRow() const;
    void type(const QString &text);

    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
    SnippetManager *m_snippets = nullptr;
    ScriptActionManager *m_scripts = nullptr;
    CommandPalette *m_palette = nullptr;
    QLineEdit *m_input = nullptr;
    QListView *m_list = nullptr;
};

void TestPaletteUi::init()
{
    const QString path = m_dir.filePath(
        QStringLiteral("palette-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
    m_snippets = new SnippetManager(m_storage->database());
    m_scripts = new ScriptActionManager();
    m_palette = new CommandPalette(m_storage);
    m_palette->setSnippetManager(m_snippets);
    m_palette->setScriptManager(m_scripts);
    m_palette->setTagCandidates({QStringLiteral("work"), QStringLiteral("private")});
    m_palette->setGroupCandidates({QStringLiteral("Invoices"), QStringLiteral("Recipes")});
    m_palette->setRecentCommands({QStringLiteral("tag"), QStringLiteral("export")});
    m_input = m_palette->findChild<QLineEdit *>();
    m_list = m_palette->findChild<QListView *>();
    QVERIFY(m_input);
    QVERIFY(m_list);
}

void TestPaletteUi::cleanup()
{
    delete m_palette;
    m_palette = nullptr;
    delete m_scripts;
    m_scripts = nullptr;
    delete m_snippets;
    m_snippets = nullptr;
    delete m_storage;
    m_storage = nullptr;
}

QModelIndex TestPaletteUi::firstRow() const
{
    return m_list->model()->index(0, 0);
}

void TestPaletteUi::type(const QString &text)
{
    m_input->setText(text); // emits textChanged, exactly like typing
}

void TestPaletteUi::listsCommandsForBarePrefix()
{
    m_palette->openPalette();
    type(QStringLiteral(">"));
    QVERIFY(m_list->model()->rowCount() > 5);
    // Recently used commands come first, then the rest.
    QCOMPARE(firstRow().data(Qt::UserRole).toString(), QStringLiteral("tag"));
    QCOMPARE(m_list->model()->index(1, 0).data(Qt::UserRole).toString(),
             QStringLiteral("export"));
    // Every command is listed exactly once.
    QSet<QString> ids;
    for (int row = 0; row < m_list->model()->rowCount(); ++row)
        ids.insert(m_list->model()->index(row, 0).data(Qt::UserRole).toString());
    QCOMPARE(ids.size(), m_list->model()->rowCount());
    QVERIFY(ids.contains(QStringLiteral("clean")));
}

void TestPaletteUi::narrowsCommandsWhileTyping()
{
    m_palette->openPalette();
    type(QStringLiteral(">de"));
    // A unique prefix resolves to the command: it acts on the selection, so the
    // hint says what Enter will do instead of listing rows.
    QCOMPARE(m_list->model()->rowCount(), 0);
    QLabel *hint = m_palette->findChild<QLabel *>();
    QVERIFY(hint);
    QVERIFY(hint->text().contains(QStringLiteral("Delete the selected entry")));

    // An ambiguous prefix offers the candidates instead of failing.
    type(QStringLiteral(">co"));
    QCOMPARE(m_list->model()->rowCount(), 2);
    QCOMPARE(firstRow().data(Qt::UserRole).toString(), QStringLiteral("copy"));

    // A command that takes an argument switches to its completion list.
    type(QStringLiteral(">tag"));
    QCOMPARE(m_list->model()->rowCount(), 2); // every tag is offered
}

void TestPaletteUi::completesTagsAndRunsTheCommand()
{
    m_palette->openPalette();
    QSignalSpy tagSpy(m_palette, &CommandPalette::tagRequested);
    QSignalSpy executedSpy(m_palette, &CommandPalette::commandExecuted);

    type(QStringLiteral(">tag ")); // every tag is offered
    QCOMPARE(m_list->model()->rowCount(), 2);
    QCOMPARE(firstRow().data(Qt::UserRole).toString(), QStringLiteral("work"));

    type(QStringLiteral(">tag work"));
    QCOMPARE(m_list->model()->rowCount(), 1);
    // Enter with an argument runs the command straight away.
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(tagSpy.count(), 1);
    QCOMPARE(tagSpy.first().first().toString(), QStringLiteral("work"));
    QCOMPARE(executedSpy.count(), 1);
    QCOMPARE(executedSpy.first().first().toString(), QStringLiteral("tag"));
}

void TestPaletteUi::completesTagsByTabWithoutRunning()
{
    m_palette->openPalette();
    QSignalSpy tagSpy(m_palette, &CommandPalette::tagRequested);

    type(QStringLiteral(">tag wo"));
    QCOMPARE(firstRow().data(Qt::UserRole).toString(), QStringLiteral("work"));
    QTest::keyClick(m_input, Qt::Key_Tab);
    QCOMPARE(m_input->text(), QStringLiteral(">tag work"));
    QCOMPARE(tagSpy.count(), 0); // Tab completes, it never executes

    // Enter on an empty argument completes instead of running with nothing.
    type(QStringLiteral(">group "));
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(m_input->text(), QStringLiteral(">group Invoices"));

    QSignalSpy groupSpy(m_palette, &CommandPalette::groupRequested);
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(groupSpy.count(), 1);
    QCOMPARE(groupSpy.first().first().toString(), QStringLiteral("Invoices"));
}

void TestPaletteUi::freeTextTagStillRuns()
{
    m_palette->openPalette();
    QSignalSpy tagSpy(m_palette, &CommandPalette::tagRequested);

    // An unknown tag name has no completion but must still be accepted.
    type(QStringLiteral(">tag brand new"));
    QCOMPARE(m_list->model()->rowCount(), 0);
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(tagSpy.count(), 1);
    QCOMPARE(tagSpy.first().first().toString(), QStringLiteral("brand new"));
}

void TestPaletteUi::argumentlessCommandEmitsForTheSelection()
{
    m_palette->openPalette();
    QSignalSpy deleteSpy(m_palette, &CommandPalette::deleteRequested);
    QSignalSpy pauseSpy(m_palette, &CommandPalette::togglePauseRequested);
    QSignalSpy settingsSpy(m_palette, &CommandPalette::settingsRequested);
    QSignalSpy cleanSpy(m_palette, &CommandPalette::clearHistoryRequested);

    type(QStringLiteral(">delete"));
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(deleteSpy.count(), 1);

    m_palette->openPalette();
    type(QStringLiteral(">pause"));
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(pauseSpy.count(), 1);

    m_palette->openPalette();
    type(QStringLiteral(">settings"));
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(settingsSpy.count(), 1);

    m_palette->openPalette();
    type(QStringLiteral(">clean"));
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(cleanSpy.count(), 1);
}

void TestPaletteUi::completesExportFormats()
{
    m_palette->openPalette();
    QSignalSpy exportSpy(m_palette, &CommandPalette::exportRequested);

    type(QStringLiteral(">export "));
    QCOMPARE(m_list->model()->rowCount(), 4);
    QCOMPARE(firstRow().data(Qt::UserRole).toString(), QStringLiteral("json"));
    type(QStringLiteral(">export mar"));
    QCOMPARE(m_list->model()->rowCount(), 1);
    QCOMPARE(firstRow().data(Qt::UserRole).toString(), QStringLiteral("markdown"));

    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(exportSpy.count(), 1);
    QCOMPARE(exportSpy.first().first().toString(), QStringLiteral("markdown"));
}

void TestPaletteUi::plainTextStillSearchesHistory()
{
    m_palette->openPalette();
    QSignalSpy executedSpy(m_palette, &CommandPalette::commandExecuted);

    // No '>' prefix: history search, and the ">" commands must stay out of it.
    ClipboardRecord record;
    record.type = ContentType::Text;
    record.hash = QByteArrayLiteral("hash-1");
    record.textData = QStringLiteral(">tag looks like a command");
    record.preview = record.textData;
    record.timestamp = QDateTime::currentMSecsSinceEpoch();
    record.sizeBytes = record.textData.size();
    m_storage->insertOrUpdate(record);

    type(QStringLiteral("tag looks like"));
    QCOMPARE(m_list->model()->rowCount(), 1);
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(executedSpy.count(), 0); // pasted, not run as a command
}

void TestPaletteUi::noArgumentCommandWithEmptyInputDoesNothing()
{
    m_palette->openPalette();
    QSignalSpy tagSpy(m_palette, &CommandPalette::tagRequested);
    type(QStringLiteral(">tag"));
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(tagSpy.count(), 0); // nothing typed, nothing to tag with
    QVERIFY(m_palette->isVisible()); // and the palette stays open
}

void TestPaletteUi::unknownCommandShowsSuggestions()
{
    m_palette->openPalette();
    type(QStringLiteral(">zzz"));
    QCOMPARE(m_list->model()->rowCount(), 0);
    QVERIFY(m_palette->isVisible());

    // A known word with no argument support behaves like the old >pin.
    QSignalSpy pinSpy(m_palette, &CommandPalette::pinRequested);
    type(QStringLiteral(">p"));
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(pinSpy.count(), 1);
    QCOMPARE(pinSpy.first().first().toLongLong(), qint64(0)); // 0 = main window's selection
}

void TestPaletteUi::emptyInputShowsRecentSearchesAndCommands()
{
    // R3: empty input lists recent searches + recent commands, capped at 10.
    m_palette->setRecentSearches({QStringLiteral("app:firefox"), QStringLiteral("has:ocr")});
    m_palette->openPalette();
    type(QString());
    QVERIFY(m_list->model()->rowCount() >= 4); // 2 searches + tag/export recents
    const auto rows = m_palette->recentRows();
    QVERIFY(!rows.isEmpty());
    QCOMPARE(rows.first().kind, QStringLiteral("search"));
    QCOMPARE(rows.first().payload, QStringLiteral("app:firefox"));
    // Picking a search row fills the input instead of running anything.
    QTest::keyClick(m_input, Qt::Key_Return);
    QCOMPARE(m_input->text(), QStringLiteral("app:firefox"));
    QVERIFY(m_palette->isVisible());

    // No recents at all: plain history again.
    m_palette->setRecentSearches({});
    m_palette->setRecentCommands({});
    m_palette->rebuildRecentRows();
    QCOMPARE(m_palette->recentRows().size(), 0);
    m_palette->openPalette();
    type(QString());
    QCOMPARE(m_palette->recentRows().size(), 0);
}

void TestPaletteUi::ghostSuffixIsTheCompletableRemainder()
{
    // R3: pure function — remainder of the candidate after the typed input.
    QCOMPARE(CommandPalette::ghostSuffix(QStringLiteral("wor"), QStringLiteral("work")),
             QStringLiteral("k"));
    QCOMPARE(CommandPalette::ghostSuffix(QStringLiteral(""), QStringLiteral("work")),
             QString());
    QCOMPARE(CommandPalette::ghostSuffix(QStringLiteral("work"), QStringLiteral("work")),
             QString());
    QCOMPARE(CommandPalette::ghostSuffix(QStringLiteral("xyz"), QStringLiteral("work")),
             QString());
    QCOMPARE(CommandPalette::ghostSuffix(QStringLiteral("WOR"), QStringLiteral("work")),
             QStringLiteral("k")); // case-insensitive prefix

    // Wired: typing a tag prefix shows the ghost hint.
    m_palette->openPalette();
    type(QStringLiteral(">tag wo"));
    QLabel *ghost = nullptr;
    for (QLabel *label : m_palette->findChildren<QLabel *>()) {
        if (label->text().startsWith(QStringLiteral("Tab:"))) {
            ghost = label;
            break;
        }
    }
    QVERIFY(ghost);
    QVERIFY(ghost->isVisible());
    QCOMPARE(ghost->text(), QStringLiteral("Tab: rk"));
}

void TestPaletteUi::historyRowsCarryTypeAndSource()
{
    // R3: history rows expose type + source app + timestamp for the delegate.
    ClipboardRecord record;
    record.type = ContentType::Image;
    record.hash = QByteArrayLiteral("hash-img");
    record.textData = QStringLiteral("screenshot");
    record.preview = QStringLiteral("screenshot");
    record.sourceApp = QStringLiteral("Spectacle");
    record.timestamp = QDateTime::currentMSecsSinceEpoch();
    record.sizeBytes = 42;
    m_storage->insertOrUpdate(record);

    m_palette->setRecentSearches({});
    m_palette->setRecentCommands({});
    m_palette->openPalette();
    type(QStringLiteral("screenshot"));
    QVERIFY(m_list->model()->rowCount() >= 1);
    const QModelIndex row = firstRow();
    QVERIFY(row.data(Qt::UserRole).toLongLong() != 0);
    QCOMPARE(row.data(Qt::UserRole + 2).toInt(), int(ContentType::Image));
    QCOMPARE(row.data(Qt::UserRole + 3).toString(), QStringLiteral("Spectacle"));
    QVERIFY(row.data(Qt::UserRole + 1).toLongLong() > 0);
}

QTEST_MAIN(TestPaletteUi)
#include "tst_paletteui.moc"
