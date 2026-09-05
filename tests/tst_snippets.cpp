#include <QtTest>
#include "SnippetManager.h"
#include "StorageManager.h"
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QRandomGenerator>

class TestSnippets : public QObject {
    Q_OBJECT
private slots:
    void init();
    void createAndFetch();
    void expandPlaceholders();
    void expandCaseInsensitiveAndWhitespace();
    void updateAndDelete();
    void expandSnippet();
    void validatesWritesAndEmitsChanges();
    void preservesUnknownPlaceholders();

private:
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
    SnippetManager *m_mgr = nullptr;
};

void TestSnippets::init()
{
    delete m_mgr;
    delete m_storage;
    const QString path = m_dir.filePath(QStringLiteral("snip-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
    m_mgr = new SnippetManager(m_storage->database());
}

void TestSnippets::createAndFetch()
{
    const qint64 id = m_mgr->createSnippet(QStringLiteral("git commit"), QStringLiteral("git commit -m \"{{clipboard}}\" [{{date}}]"), QStringLiteral("gcm"));
    QVERIFY(id != 0);
    auto s = m_mgr->snippet(id);
    QVERIFY(s.has_value());
    QCOMPARE(s->name, QStringLiteral("git commit"));
    QCOMPARE(s->templateText, QStringLiteral("git commit -m \"{{clipboard}}\" [{{date}}]"));
    QCOMPARE(m_mgr->snippets().size(), 1);
}

void TestSnippets::expandPlaceholders()
{
    const QString out = SnippetManager::expand(QStringLiteral("hi {{clipboard}} [{{date}}] {{time}} {{datetime}} {{timestamp}}"), QStringLiteral("world"));
    QVERIFY(out.startsWith(QStringLiteral("hi world [")));
    QVERIFY(out.contains(QStringLiteral("world")));
    // timestamp is numeric
    QVERIFY(out.contains(QRegularExpression(QStringLiteral("\\d{13}"))));
}

void TestSnippets::expandCaseInsensitiveAndWhitespace()
{
    QCOMPARE(SnippetManager::expand(QStringLiteral("x{{ clipboard }}y"), QStringLiteral("A")), QStringLiteral("xAy"));
    QCOMPARE(SnippetManager::expand(QStringLiteral("{{TEXT}}"), QStringLiteral("hi")), QStringLiteral("hi"));
    QCOMPARE(SnippetManager::expand(QStringLiteral("{{selection}}"), QStringLiteral("foo")), QStringLiteral("foo"));
}

void TestSnippets::updateAndDelete()
{
    const qint64 id = m_mgr->createSnippet(QStringLiteral("a"), QStringLiteral("t1"), {});
    QVERIFY(m_mgr->updateSnippet(id, QStringLiteral("a2"), QStringLiteral("t2"), QStringLiteral("s")));
    QCOMPARE(m_mgr->snippet(id)->name, QStringLiteral("a2"));
    QVERIFY(m_mgr->deleteSnippet(id));
    QVERIFY(!m_mgr->snippet(id).has_value());
    QCOMPARE(m_mgr->snippets().size(), 0);
}

void TestSnippets::expandSnippet()
{
    const qint64 id = m_mgr->createSnippet(QStringLiteral("wrap"), QStringLiteral("[{{clipboard}}]"), {});
    const QString out = m_mgr->expandSnippet(id, QStringLiteral("hello"));
    QCOMPARE(out, QStringLiteral("[hello]"));
    QVERIFY(m_mgr->expandSnippet(99999, QStringLiteral("hi")).isEmpty());
}

void TestSnippets::validatesWritesAndEmitsChanges()
{
    QSignalSpy changedSpy(m_mgr, &SnippetManager::snippetsChanged);

    QCOMPARE(m_mgr->createSnippet(QStringLiteral("   "), QStringLiteral("template")), qint64(0));
    QCOMPARE(m_mgr->createSnippet(QStringLiteral("name"), QString()), qint64(0));
    QCOMPARE(changedSpy.count(), 0);

    const qint64 id = m_mgr->createSnippet(QStringLiteral("  trimmed name  "), QStringLiteral("template"),
                                           QStringLiteral("  shortcut  "));
    QVERIFY(id > 0);
    QCOMPARE(changedSpy.count(), 1);
    const auto created = m_mgr->snippet(id);
    QVERIFY(created.has_value());
    QCOMPARE(created->name, QStringLiteral("trimmed name"));
    QCOMPARE(created->shortcut, QStringLiteral("shortcut"));

    QVERIFY(m_mgr->updateSnippet(id, QStringLiteral("  updated  "), QStringLiteral("new template"),
                                 QStringLiteral("  new  ")));
    QCOMPARE(changedSpy.count(), 2);
    const auto updated = m_mgr->snippet(id);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->name, QStringLiteral("updated"));
    QCOMPARE(updated->shortcut, QStringLiteral("new"));

    QVERIFY(!m_mgr->updateSnippet(99999, QStringLiteral("missing"), QStringLiteral("template"), {}));
    QVERIFY(!m_mgr->deleteSnippet(99999));
    QCOMPARE(changedSpy.count(), 2);
    QVERIFY(m_mgr->deleteSnippet(id));
    QCOMPARE(changedSpy.count(), 3);
}

void TestSnippets::preservesUnknownPlaceholders()
{
    QCOMPARE(SnippetManager::expand(QStringLiteral("{{unknown}} / {{ clipboard }} / {{x-y}}"),
                                    QStringLiteral("value")),
             QStringLiteral("{{unknown}} / value / {{x-y}}"));
    QCOMPARE(SnippetManager::expand(QString(), QStringLiteral("ignored")), QString());
    QCOMPARE(SnippetManager::expand(QStringLiteral("literal text"), QStringLiteral("ignored")),
             QStringLiteral("literal text"));
}

QTEST_GUILESS_MAIN(TestSnippets)
#include "tst_snippets.moc"
