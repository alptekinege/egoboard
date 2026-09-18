#include <QtTest>

#include "DatabaseSchema.h"
#include "SearchEngine.h"
#include "StorageManager.h"

#include <QDate>
#include <QDateTime>
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
    void ftsUpdatesWhenOcrTextChanges();
    void combinesSearchWithMetadataFilters();
    void parsesFieldFilters();
    void parsesFreeTextPhrasesAndExclusions();
    void reportsUnusableFieldValues();
    void buildFtsQueryKeepsPhrases();
    void stripQueryQuotesRemovesOnlyQuoting();
    void negatedTermsRoundTrip();
    void appliesExclusionsAndOcrFilter();

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

void TestSearchEngine::ftsUpdatesWhenOcrTextChanges()
{
    ClipboardRecord image;
    image.hash = QByteArrayLiteral("ocr_update");
    image.type = ContentType::Image;
    image.blobData = QByteArrayLiteral("image");
    image.hasBlob = true;
    image.preview = QStringLiteral("Screenshot");
    image.timestamp = 1000;
    const qint64 id = m_storage->insertOrUpdate(image);
    QVERIFY(id > 0);

    QVERIFY(m_storage->setOcrText(id, QStringLiteral("initial recognized text")));
    FilterSpec initial;
    initial.searchText = QStringLiteral("initial");
    QCOMPARE(m_storage->fetchPage(initial, {}, 10).size(), 1);

    QVERIFY(m_storage->setOcrText(id, QStringLiteral("replacement recognized text")));
    QCOMPARE(m_storage->fetchPage(initial, {}, 10).size(), 0);
    FilterSpec replacement;
    replacement.searchText = QStringLiteral("replacement");
    QCOMPARE(m_storage->fetchPage(replacement, {}, 10).size(), 1);

    QVERIFY(m_storage->setOcrText(id, QString()));
    QCOMPARE(m_storage->fetchPage(replacement, {}, 10).size(), 0);
}

void TestSearchEngine::combinesSearchWithMetadataFilters()
{
    ClipboardRecord matching;
    matching.hash = QByteArrayLiteral("combined-match");
    matching.type = ContentType::Text;
    matching.textData = QStringLiteral("shared phrase");
    matching.preview = matching.textData;
    matching.timestamp = 2000;
    matching.sourceApp = QStringLiteral("terminal");
    matching.pinned = true;
    m_storage->insertOrUpdate(matching);

    ClipboardRecord wrongApp = matching;
    wrongApp.hash = QByteArrayLiteral("combined-wrong-app");
    wrongApp.sourceApp = QStringLiteral("browser");
    m_storage->insertOrUpdate(wrongApp);

    ClipboardRecord wrongPin = matching;
    wrongPin.hash = QByteArrayLiteral("combined-wrong-pin");
    wrongPin.pinned = false;
    m_storage->insertOrUpdate(wrongPin);

    FilterSpec filter;
    filter.searchText = QStringLiteral("shared");
    filter.sourceApp = QStringLiteral("terminal");
    filter.pinnedOnly = true;
    filter.fromMs = 2000;
    filter.toMs = 2000;
    const auto results = m_storage->fetchPage(filter, {}, 10);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().hash, QByteArrayLiteral("combined-match"));
}

void TestSearchEngine::parsesFieldFilters()
{
    FilterSpec base;
    base.sortMode = FilterSpec::SortMode::MostUsed;
    const auto parsed = SearchEngine::parseQuery(
        QStringLiteral("app:firefox type:image tag:work tag:urgent pinned:yes has:ocr report"),
        base);

    QCOMPARE(parsed.filter.sourceApp, QStringLiteral("firefox"));
    QCOMPARE(parsed.filter.contentType, int(ContentType::Image));
    QCOMPARE(parsed.filter.tags,
             QStringList({QStringLiteral("work"), QStringLiteral("urgent")}));
    QVERIFY(parsed.filter.pinnedOnly);
    QVERIFY(parsed.filter.hasOcrOnly);
    QCOMPARE(parsed.filter.searchText, QStringLiteral("report"));
    QCOMPARE(parsed.filter.sortMode, FilterSpec::SortMode::MostUsed); // untouched base field
    QVERIFY(parsed.problems.isEmpty());
    QCOMPARE(parsed.applied.size(), 6);

    // Quoted field values keep their spaces; dates become bounds.
    const auto dated = SearchEngine::parseQuery(
        QStringLiteral("app:\"Visual Studio Code\" before:2024-01-31 after:7d"));
    QCOMPARE(dated.filter.sourceApp, QStringLiteral("Visual Studio Code"));
    // before: a date is the end of that day; after: a relative value is now - n.
    QCOMPARE(QDateTime::fromMSecsSinceEpoch(dated.filter.toMs).date(), QDate(2024, 1, 31));
    const qint64 sevenDaysAgo = QDateTime::currentMSecsSinceEpoch() - 7 * 24 * 60 * 60 * 1000;
    QVERIFY(qAbs(dated.filter.fromMs - sevenDaysAgo) < 60 * 1000);
    QCOMPARE(dated.filter.searchText, QString());

    // pinned:no clears a toolbar preset rather than being ignored.
    FilterSpec pinnedBase;
    pinnedBase.pinnedOnly = true;
    const auto unpinned = SearchEngine::parseQuery(QStringLiteral("pinned:no"), pinnedBase);
    QVERIFY(!unpinned.filter.pinnedOnly);
}

void TestSearchEngine::parsesFreeTextPhrasesAndExclusions()
{
    const auto parsed = SearchEngine::parseQuery(
        QStringLiteral("\"exact phrase\" invoice -draft -\"not wanted\" https://example.com"));
    QCOMPARE(parsed.text, QStringLiteral("\"exact phrase\" invoice https://example.com"));
    QCOMPARE(parsed.filter.searchText, parsed.text);
    QCOMPARE(parsed.filter.excludeText, QStringLiteral("draft \"not wanted\""));
    QVERIFY(parsed.problems.isEmpty());

    // A lone dash is ordinary text, not an exclusion.
    const auto dash = SearchEngine::parseQuery(QStringLiteral("-"));
    QCOMPARE(dash.text, QStringLiteral("-"));
    QCOMPARE(dash.filter.excludeText, QString());
}

void TestSearchEngine::reportsUnusableFieldValues()
{
    const auto parsed = SearchEngine::parseQuery(
        QStringLiteral("type:video pinned:maybe has:image before:nonsense keep"));
    QCOMPARE(parsed.filter.contentType, -1);
    QVERIFY(!parsed.filter.hasOcrOnly);
    QVERIFY(!parsed.filter.pinnedOnly);
    QCOMPARE(parsed.filter.toMs, qint64(0));
    QCOMPARE(parsed.problems.size(), 4);
    QCOMPARE(parsed.text, QStringLiteral("keep"));
}

void TestSearchEngine::buildFtsQueryKeepsPhrases()
{
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("\"one two\"")),
             QStringLiteral("\"one two\"*"));
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("alpha \"one two\" beta")),
             QStringLiteral("\"alpha\"* AND \"one two\"* AND \"beta\"*"));
    // Inner quotes are escaped, inner whitespace collapses.
    QCOMPARE(SearchEngine::buildFtsQuery(QStringLiteral("\"say \"hi\"  now\"")),
             QStringLiteral("\"say \"\"hi\"\" now\"*"));
}

void TestSearchEngine::stripQueryQuotesRemovesOnlyQuoting()
{
    QCOMPARE(SearchEngine::stripQueryQuotes(QStringLiteral("\"exact phrase\" word")),
             QStringLiteral("exact phrase word"));
    QCOMPARE(SearchEngine::stripQueryQuotes(QStringLiteral("no quotes here")),
             QStringLiteral("no quotes here"));
    QCOMPARE(SearchEngine::stripQueryQuotes(QStringLiteral("  spaced   out  ")),
             QStringLiteral("spaced out"));
}

void TestSearchEngine::negatedTermsRoundTrip()
{
    // A saved search restores its non-widget fields through the search box;
    // negatedTerms() and parseQuery() must round-trip them.
    QCOMPARE(SearchEngine::negatedTerms(QStringLiteral("draft \"not now\"")),
             QStringLiteral("-draft -\"not now\""));

    const auto parsed = SearchEngine::parseQuery(
        QStringLiteral("report has:ocr ")
        + SearchEngine::negatedTerms(QStringLiteral("draft \"not now\"")));
    QVERIFY(parsed.filter.hasOcrOnly);
    QCOMPARE(parsed.filter.searchText, QStringLiteral("report"));
    QCOMPARE(parsed.filter.excludeText, QStringLiteral("draft \"not now\""));
}

void TestSearchEngine::appliesExclusionsAndOcrFilter()
{
    ClipboardRecord keep;
    keep.hash = QByteArrayLiteral("keep");
    keep.type = ContentType::Text;
    keep.textData = QStringLiteral("invoice 42 paid");
    keep.preview = keep.textData;
    keep.timestamp = 1000;
    const qint64 keepId = m_storage->insertOrUpdate(keep);

    ClipboardRecord drop;
    drop.hash = QByteArrayLiteral("drop");
    drop.type = ContentType::Text;
    drop.textData = QStringLiteral("invoice 43 draft");
    drop.preview = drop.textData;
    drop.timestamp = 2000;
    m_storage->insertOrUpdate(drop);

    // An entry with no text at all must survive an exclusion (NULL NOT LIKE).
    ClipboardRecord image;
    image.hash = QByteArrayLiteral("image");
    image.type = ContentType::Image;
    image.blobData = QByteArrayLiteral("png");
    image.hasBlob = true;
    image.preview = QStringLiteral("Screenshot");
    image.timestamp = 3000;
    m_storage->insertOrUpdate(image);

    FilterSpec exclude;
    exclude.searchText = QStringLiteral("invoice");
    exclude.excludeText = QStringLiteral("draft");
    const auto excluded = m_storage->fetchPage(exclude, {}, 10);
    QCOMPARE(excluded.size(), 1);
    QCOMPARE(excluded.first().hash, QByteArrayLiteral("keep"));

    FilterSpec excludeOnly;
    excludeOnly.excludeText = QStringLiteral("draft");
    QCOMPARE(m_storage->fetchPage(excludeOnly, {}, 10).size(), 2);

    // Phrase search requires the words to be adjacent.
    FilterSpec phrase;
    phrase.searchText = QStringLiteral("\"42 paid\"");
    QCOMPARE(m_storage->fetchPage(phrase, {}, 10).size(), 1);
    FilterSpec notAdjacent;
    notAdjacent.searchText = QStringLiteral("\"paid 42\"");
    QCOMPARE(m_storage->fetchPage(notAdjacent, {}, 10).size(), 0);

    // has:ocr only matches entries carrying OCR text.
    FilterSpec ocr;
    ocr.hasOcrOnly = true;
    QCOMPARE(m_storage->fetchPage(ocr, {}, 10).size(), 0);
    QVERIFY(m_storage->setOcrText(keepId, QStringLiteral("recognized")));
    const auto withOcr = m_storage->fetchPage(ocr, {}, 10);
    QCOMPARE(withOcr.size(), 1);
    QCOMPARE(withOcr.first().id, keepId);
}

QTEST_GUILESS_MAIN(TestSearchEngine)
#include "tst_searchengine.moc"
