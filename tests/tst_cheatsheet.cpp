#include <QtTest>

#include "ShortcutCheatsheet.h"

#include <QTableWidget>

class TestCheatsheet : public QObject
{
    Q_OBJECT

private slots:
    void defaultSectionsCoverEveryArea();
    void dialogBuildsAllRows();
};

void TestCheatsheet::defaultSectionsCoverEveryArea()
{
    // U15: system, window, quick-paste, palette and timeline areas, every row
    // with both a key combo and a description.
    const QList<ShortcutCheatsheet::Section> sections = ShortcutCheatsheet::defaultSections();
    QCOMPARE(sections.size(), 5);
    int totalRows = 0;
    bool hasMetaV = false;
    bool hasCtrlK = false;
    bool hasQuestion = false;
    for (const auto &section : sections) {
        QVERIFY(!section.title.isEmpty());
        QVERIFY(!section.rows.isEmpty());
        for (const auto &[keys, action] : section.rows) {
            QVERIFY(!keys.isEmpty());
            QVERIFY(!action.isEmpty());
            hasMetaV = hasMetaV || keys == QStringLiteral("Meta+V");
            hasCtrlK = hasCtrlK || keys == QStringLiteral("Ctrl+K");
            hasQuestion = hasQuestion || keys == QStringLiteral("?");
            ++totalRows;
        }
    }
    QVERIFY(hasMetaV);
    QVERIFY(hasCtrlK);
    QVERIFY(hasQuestion);
    QVERIFY(totalRows >= 20);
}

void TestCheatsheet::dialogBuildsAllRows()
{
    const QList<ShortcutCheatsheet::Section> sections = ShortcutCheatsheet::defaultSections();
    int expected = 0;
    for (const auto &section : sections)
        expected += 1 + section.rows.size();
    ShortcutCheatsheet dialog(sections);
    dialog.show();
    QTest::qWait(20);
    const QList<QTableWidget *> tables = dialog.findChildren<QTableWidget *>();
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables.first()->rowCount(), expected);
    QVERIFY(!tables.first()->accessibleName().isEmpty());
    dialog.close();
}

QTEST_MAIN(TestCheatsheet)
#include "tst_cheatsheet.moc"
