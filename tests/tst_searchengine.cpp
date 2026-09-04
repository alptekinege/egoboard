#include <QtTest>

#include "DatabaseSchema.h"
#include "SearchEngine.h"
#include "StorageManager.h"

#include <QRandomGenerator>
#include <QSqlQuery>
#include <QTemporaryDir>

class TestSearchEngine : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void buildFtsQueryEmptyAndWhitespace();
    void buildFtsQuerySingleToken();
    void buildFtsQueryMultiToken();
    void buildFtsQueryPunctuationHandling();
    void buildFtsQueryQuoteEscaping();
    void buildFtsQueryTruncation();
    void likeEscapeRules();
    void ftsAvailabilityAndExecution();
    void ftsMatchesOcrText();
    void ftsTriggersOnUpdateAndDelete();

private:
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
};

void TestSearchEngine::init()
{
    delete m_storage;
    const QString path = m_dir.filePath(
        QStringLiteral("search-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
}

void TestSearchEngine::buildFtsQueryEmptyAndWhitespace()
{
    QCOMPARE(SearchEngine::buildFtsQuery(QString()), QString());
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("")), QString());
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("   \t\n  ")), QString());
}

void TestSearchEngine::buildFtsQuerySingleToken()
{
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("hello")),
             QStringLiteral("\"hello\"*"));
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("   clipboard  ")),
             QStringLiteral("\"clipboard\"*"));
}

void TestSearchEngine::buildFtsQueryMultiToken()
{
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("hello world")),
             QStringLiteral("\"hello\"* AND \"world\"*"));
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("qt6   kde   wayland")),
             QStringLiteral("\"qt6\"* AND \"kde\"* AND \"wayland\"*"));
}

void TestSearchEngine::buildFtsQueryPunctuationHandling()
{
    // Strips leading/trailing punctuation from tokens
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("(hello), [world]!")),
             QStringLiteral("\"hello\"* AND \"world\"*"));

    // Tokens without any letter or digit are discarded entirely
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("... ;;; !!! ???")), QString());

    // Mix of punctuation and words
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("--- #tag1 @user2 ---")),
             QStringLiteral("\"tag1\"* AND \"user2\"*"));
}

void TestSearchEngine::buildFtsQueryQuoteEscaping()
{
    // Quotes inside a token are doubled for FTS5 string escaping
    const QString query = SearchEngine::buildFtsQuery(QStringLiteral("abc\"def"));
    QCOMPARE(query, QStringLiteral("\"abc\"\"def\"*"));
}

void TestSearchEngine::buildFtsQueryTruncation()
{
    // Tokens over 64 characters are capped at 64
    const QString longToken = QStringLiteral("a").repeated(100);
    const QString query = SearchEngine::buildFtsQuery(longToken);
    QCOMPARE(query, QStringLiteral("\"%1\"*").arg(QStringLiteral("a").repeated(64)));
}

void TestSearchEngine::likeEscapeRules()
{
    QCOMPARE(SearchEngine::likeEscape(QStringLiteral("normal text")), QStringLiteral("normal text"));
    QCOMPARE(SearchEngine::likeEscape(QStringLiteral("100% discount")), QStringLiteral("100\\% discount"));
    QCOMPARE(SearchEngine::likeEscape(QStringLiteral("user_name")), QStringLiteral("user\\_name"));
    QCOMPARE(SearchEngine::likeEscape(QStringLiteral("path\\to\\file")), QStringLiteral("path\\\\to\\\\file"));
    QCOMPARE(SearchEngine::likeEscape(QStringLiteral("%_\\")), QStringLiteral("\\%\\_\\\\"));
}

void TestSearchEngine::ftsAvailabilityAndExecution()
{
    QVERIFY(SearchEngine::isFtsAvailable(m_storage->database()));

    ClipboardRecord rec1;
    rec1.hash = QByteArrayLiteral("h1");
    rec1.type = ContentType::Text;
    rec1.textData = QStringLiteral("The quick brown fox jumps over the lazy dog");
    rec1.preview = QStringLiteral("The quick brown fox");
    rec1.timestamp = 1000;
    m_storage->insertOrUpdate(rec1);

    ClipboardRecord rec2;
    rec2.hash = QByteArrayLiteral("h2");
    rec2.type = ContentType::Text;
    rec2.textData = QStringLiteral("C++20 development with Qt 6 and KDE Frameworks");
    rec2.preview = QStringLiteral("C++20 development");
    rec2.timestamp = 2000;
    m_storage->insertOrUpdate(rec2);

    // Prefix search for "quick"
    FilterSpec f1;
    f1.searchText = QStringLiteral("qui");
    auto results1 = m_storage->fetchPage(f1, {}, 10);
    QCOMPARE(results1.size(), 1);
    QCOMPARE(results1.first().preview, QStringLiteral("The quick brown fox"));

    // Multi-term search
    FilterSpec f2;
    f2.searchText = QStringLiteral("qt frameworks");
    auto results2 = m_storage->fetchPage(f2, {}, 10);
    QCOMPARE(results2.size(), 1);
    QCOMPARE(results2.first().preview, QStringLiteral("C++20 development"));

    // No match
    FilterSpec f3;
    f3.searchText = QStringLiteral("nonexistentword123");
    auto results3 = m_storage->fetchPage(f3, {}, 10);
    QCOMPARE(results3.size(), 0);
}

void TestSearchEngine::ftsMatchesOcrText()
{
    ClipboardRecord image;
    image.hash = QByteArrayLiteral("img_ocr");
    image.type = ContentType::Image;
    image.blobData = QByteArrayLiteral("fakepngdata");
    image.hasBlob = true;
    image.preview = QStringLiteral("Screenshot 1");
    image.timestamp = 3000;
    const qint64 id = m_storage->insertOrUpdate(image);
    QVERIFY(id > 0);

    // Set OCR text
    QVERIFY(m_storage->setOcrText(id, QStringLiteral("Invoice #4092 Paid Successfully")));

    // Search query matching only the OCR text
    FilterSpec filter;
    filter.searchText = QStringLiteral("Invoice 4092");
    auto results = m_storage->fetchPage(filter, {}, 10);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, id);
}

void TestSearchEngine::ftsTriggersOnUpdateAndDelete()
{
    ClipboardRecord rec;
    rec.hash = QByteArrayLiteral("touch_fts");
    rec.type = ContentType::Text;
    rec.textData = QStringLiteral("Original content alpha");
    rec.preview = QStringLiteral("Original content alpha");
    rec.timestamp = 1000;
    rec.sourceApp = QStringLiteral("app1");
    const qint64 id = m_storage->insertOrUpdate(rec);
    QVERIFY(id > 0);

    // Verify search matches
    FilterSpec f1;
    f1.searchText = QStringLiteral("alpha");
    QCOMPARE(m_storage->fetchPage(f1, {}, 10).size(), 1);

    // Touch via deduplication update with new sourceApp
    rec.timestamp = 2000;
    rec.sourceApp = QStringLiteral("app2");
    bool updated = false;
    m_storage->insertOrUpdate(rec, &updated);
    QVERIFY(updated);

    // Search still matches after update
    QCOMPARE(m_storage->fetchPage(f1, {}, 10).size(), 1);

    // Delete and verify FTS trigger deleted it
    QVERIFY(m_storage->remove(id));
    QCOMPARE(m_storage->fetchPage(f1, {}, 10).size(), 0);
}

QTEST_GUILESS_MAIN(TestSearchEngine)
#include "tst_searchengine.moc"
