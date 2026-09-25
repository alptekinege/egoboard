#include <QtTest>

#include "ExpirePolicy.h"
#include "ExpireScheduler.h"
#include "SettingsManager.h"
#include "StorageManager.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

// Rule codec + end-to-end expiry against a real temp database.
class TestExpire : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void roundTripThroughListCodec();
    void parsesHumanAges();
    void rejectsMalformedRules();
    void appliesRuleAgainstStorage();
    void schedulerAppliesConfiguredRulesAndEmitsSignal();
    void schedulerStartAppliesRules();
    void rejectsNonPositiveAndOverflowAges();
    void decodesOnlyValidRules();
    void schedulerIgnoresInvalidAndEmptyRules();
    void parsesNumericAndNamedTypes();
    void parseBoolVariants();
    void rejectsExtraFieldsAndPipeInWildcard();
    void toStringMasksInvalidTypeAsAny();
    void decodePreservesOrderAndDropsInvalid();
    void parsesAgeEdgeForms();
    void cutoffBoundaryIsStrictlyOlder();
    void questionMarkAndEscapedWildcards();
    void emptyWildcardMatchesNullSourceApp();
    void multiRuleUnionKeepsBroadest();
    void keepPinnedFalseOverridesProtection();
    void overlappingRulesCountOnce();
    void noSignalWithoutVictims();

private:
    ClipboardRecord makeRecord(const QByteArray &hash, const QString &text, qint64 timestamp);
    QTemporaryDir m_dir;
};

ClipboardRecord TestExpire::makeRecord(const QByteArray &hash, const QString &text, qint64 timestamp)
{
    ClipboardRecord record;
    record.hash = hash;
    record.type = ContentType::Text;
    record.textData = text;
    record.preview = text.left(40);
    record.timestamp = timestamp;
    record.sizeBytes = text.size();
    return record;
}

void TestExpire::initTestCase()
{
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestExpire::roundTripThroughListCodec()
{
    QList<ExpireRule> rules;
    ExpireRule terminal;
    terminal.contentType = int(ContentType::Text);
    terminal.sourceAppWildcard = QStringLiteral("konsole*");
    terminal.ageSeconds = 24 * 3600;
    terminal.keepPinned = true;
    rules.append(terminal);

    ExpireRule images;
    images.contentType = int(ContentType::Image);
    images.ageSeconds = 7 * 86400;
    images.keepPinned = false;
    rules.append(images);

    const QList<ExpireRule> decoded = decodeRules(encodeRules(rules));
    QCOMPARE(decoded.size(), 2);

    QCOMPARE(decoded.at(0).contentType, int(ContentType::Text));
    QCOMPARE(decoded.at(0).sourceAppWildcard, QStringLiteral("konsole*"));
    QCOMPARE(decoded.at(0).ageSeconds, qint64(24 * 3600));
    QVERIFY(decoded.at(0).keepPinned);

    QCOMPARE(decoded.at(1).contentType, int(ContentType::Image));
    QCOMPARE(decoded.at(1).sourceAppWildcard, QString());
    QCOMPARE(decoded.at(1).ageSeconds, qint64(7 * 86400));
    QVERIFY(!decoded.at(1).keepPinned);

    // Any-type rule round-trips to "any".
    ExpireRule any;
    any.ageSeconds = 60;
    QCOMPARE(ExpireRule::fromString(any.toString()).contentType, -1);
}

void TestExpire::parsesHumanAges()
{
    QCOMPARE(ExpireRule::fromString(QStringLiteral("any||90m|1")).ageSeconds, qint64(90 * 60));
    QCOMPARE(ExpireRule::fromString(QStringLiteral("any||7d|0")).ageSeconds, qint64(7 * 86400));
    QCOMPARE(ExpireRule::fromString(QStringLiteral("any||12h|1")).ageSeconds, qint64(12 * 3600));
    QCOMPARE(ExpireRule::fromString(QStringLiteral("any||30s|1")).ageSeconds, qint64(30));
    // Plain seconds, no suffix.
    QCOMPARE(ExpireRule::fromString(QStringLiteral("any||86400|1")).ageSeconds, qint64(86400));
    // Case-insensitive unit + optional spacing.
    QCOMPARE(ExpireRule::fromString(QStringLiteral("any||2 D|1")).ageSeconds, qint64(2 * 86400));
}

void TestExpire::rejectsMalformedRules()
{
    QVERIFY(!ExpireRule::fromString(QStringLiteral("")).isValid());
    QVERIFY(!ExpireRule::fromString(QStringLiteral("text|firefox*|1")).isValid()); // 3 fields
    QVERIFY(!ExpireRule::fromString(QStringLiteral("text|firefox*|abc|1")).isValid()); // bad age
    QVERIFY(!ExpireRule::fromString(QStringLiteral("text|firefox*|-5|1")).isValid()); // negative age
    // Unknown type name degrades to "any" but stays valid.
    QCOMPARE(ExpireRule::fromString(QStringLiteral("bogus||60|1")).contentType, -1);
}

void TestExpire::appliesRuleAgainstStorage()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);

    const auto now = QDateTime::currentMSecsSinceEpoch();
    ClipboardRecord old = makeRecord(QByteArrayLiteral("old"), QStringLiteral("old copy"), now - 3 * 3600 * 1000);
    old.sourceApp = QStringLiteral("org.kde.konsole");
    storage.insertOrUpdate(old);

    ClipboardRecord oldPinned = makeRecord(QByteArrayLiteral("oldpin"), QStringLiteral("pinned copy"), now - 3 * 3600 * 1000);
    oldPinned.sourceApp = QStringLiteral("org.kde.konsole");
    const qint64 pinnedId = storage.insertOrUpdate(oldPinned);
    QVERIFY(storage.setPinned(pinnedId, true));

    ClipboardRecord fresh = makeRecord(QByteArrayLiteral("fresh"), QStringLiteral("fresh copy"), now - 10 * 1000);
    fresh.sourceApp = QStringLiteral("org.kde.konsole");
    storage.insertOrUpdate(fresh);

    ClipboardRecord otherApp = makeRecord(QByteArrayLiteral("other"), QStringLiteral("other app"), now - 3 * 3600 * 1000);
    otherApp.sourceApp = QStringLiteral("org.kde.kate");
    storage.insertOrUpdate(otherApp);

    QCOMPARE(storage.stats().entryCount, qint64(4));

    ExpireRule terminal;
    terminal.contentType = int(ContentType::Text);
    terminal.sourceAppWildcard = QStringLiteral("org.kde.konsole*");
    terminal.ageSeconds = 2 * 3600; // older than 2h
    QVERIFY(terminal.isValid());

    const qint64 cutoff = now - terminal.ageSeconds * 1000;
    const int removed = storage.expireEntries(cutoff, terminal.contentType,
                                              terminal.sourceAppWildcard, terminal.keepPinned);
    QCOMPARE(removed, 1); // unpinned konsole copy; pinned + fresh + kate survive
    QCOMPARE(storage.stats().entryCount, qint64(3));
}

void TestExpire::schedulerAppliesConfiguredRulesAndEmitsSignal()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-sched-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);
    SettingsManager settings;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 2 old unpinned entries (older than 1h)
    storage.insertOrUpdate(makeRecord(QByteArrayLiteral("o1"), QStringLiteral("old 1"), now - 7200 * 1000));
    storage.insertOrUpdate(makeRecord(QByteArrayLiteral("o2"), QStringLiteral("old 2"), now - 7200 * 1000));

    // 1 old pinned entry (older than 1h)
    const qint64 pinnedId = storage.insertOrUpdate(makeRecord(QByteArrayLiteral("op"), QStringLiteral("old pinned"), now - 7200 * 1000));
    storage.setPinned(pinnedId, true);

    // 1 fresh unpinned entry
    storage.insertOrUpdate(makeRecord(QByteArrayLiteral("fresh"), QStringLiteral("fresh"), now));

    QCOMPARE(storage.stats().entryCount, qint64(4));

    // Configure rule in settings: delete entries older than 3600s, keep pinned
    ExpireRule rule;
    rule.contentType = -1; // any
    rule.ageSeconds = 3600;
    rule.keepPinned = true;
    settings.setExpireRules({rule});

    ExpireScheduler scheduler(&storage, &settings);
    QSignalSpy spy(&scheduler, &ExpireScheduler::expired);

    scheduler.applyRules();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 2);
    QCOMPARE(storage.stats().entryCount, qint64(2));
}

void TestExpire::schedulerStartAppliesRules()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-start-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);
    SettingsManager settings;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    storage.insertOrUpdate(makeRecord(QByteArrayLiteral("s1"), QStringLiteral("old"), now - 7200 * 1000));

    ExpireRule rule;
    rule.ageSeconds = 3600;
    settings.setExpireRules({rule});

    ExpireScheduler scheduler(&storage, &settings);
    QSignalSpy spy(&scheduler, &ExpireScheduler::expired);

    // Calling start() immediately applies rules to catch aged-out items from when the app was closed
    scheduler.start();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 1);
    QCOMPARE(storage.stats().entryCount, qint64(0));
}

void TestExpire::rejectsNonPositiveAndOverflowAges()
{
    const QStringList invalid = {
        QStringLiteral("any||0|1"),
        QStringLiteral("any||0s|1"),
        QStringLiteral("any||-1|1"),
        QStringLiteral("any||1w|1"),
        QStringLiteral("any||999999999999999999999999d|1")
    };
    for (const QString &encoded : invalid) {
        const ExpireRule rule = ExpireRule::fromString(encoded);
        QVERIFY2(!rule.isValid(), qPrintable(encoded));
    }

    ExpireRule zero;
    QVERIFY(!zero.isValid());
    QCOMPARE(zero.toString(), QStringLiteral("any||0|1"));
}

void TestExpire::decodesOnlyValidRules()
{
    const QStringList encoded = {
        QStringLiteral("text|terminal*|60|1"),
        QStringLiteral("broken"),
        QStringLiteral("any||0|1"),
        QStringLiteral("image||2h|0")
    };
    const QList<ExpireRule> decoded = decodeRules(encoded);
    QCOMPARE(decoded.size(), 2);
    QCOMPARE(decoded.at(0).contentType, int(ContentType::Text));
    QCOMPARE(decoded.at(0).ageSeconds, qint64(60));
    QCOMPARE(decoded.at(1).contentType, int(ContentType::Image));
    QCOMPARE(decoded.at(1).ageSeconds, qint64(2 * 3600));
    QVERIFY(!decoded.at(1).keepPinned);
}

void TestExpire::schedulerIgnoresInvalidAndEmptyRules()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-invalid-sched-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);
    SettingsManager settings;
    storage.insertOrUpdate(makeRecord(QByteArrayLiteral("invalid-rule"), QStringLiteral("old"), 1));

    ExpireRule invalid;
    invalid.ageSeconds = 0;
    settings.setExpireRules({invalid});
    ExpireScheduler scheduler(&storage, &settings);
    QSignalSpy spy(&scheduler, &ExpireScheduler::expired);
    scheduler.applyRules();
    QCOMPARE(spy.count(), 0);
    QCOMPARE(storage.stats().entryCount, qint64(1));

    settings.setExpireRules({});
    scheduler.applyRules();
    QCOMPARE(spy.count(), 0);
    QCOMPARE(storage.stats().entryCount, qint64(1));
}

void TestExpire::parsesNumericAndNamedTypes()
{
    // Hand-written numeric enum values are accepted inside the range.
    QCOMPARE(ExpireRule::fromString(QStringLiteral("0||60|1")).contentType,
             int(ContentType::Text));
    QCOMPARE(ExpireRule::fromString(QStringLiteral("2||60|1")).contentType,
             int(ContentType::Image));
    // Out-of-range numerics are invalid and dropped by the list codec.
    QVERIFY(!ExpireRule::fromString(QStringLiteral("99||60|1")).isValid());
    QVERIFY(!ExpireRule::fromString(QStringLiteral("-2||60|1")).isValid());
    QCOMPARE(decodeRules(QStringList{QStringLiteral("99||60|1"),
                                     QStringLiteral("text||60|1")})
                 .size(),
             1);
    // Names are case-insensitive and whitespace-tolerant; unknown stays "any".
    QCOMPARE(ExpireRule::fromString(QStringLiteral("ANY||60|1")).contentType, -1);
    QCOMPARE(ExpireRule::fromString(QStringLiteral(" Text ||60|1")).contentType,
             int(ContentType::Text));
    QCOMPARE(ExpireRule::fromString(QStringLiteral(" IMAGE ||60|1")).contentType,
             int(ContentType::Image));
}

void TestExpire::parseBoolVariants()
{
    const QStringList truthy{QStringLiteral("1"), QStringLiteral("true"),
                             QStringLiteral("yes"), QStringLiteral("on"),
                             QStringLiteral("TRUE"), QStringLiteral("YES"),
                             QStringLiteral("ON"), QStringLiteral(" on ")};
    for (const QString &flag : truthy) {
        const ExpireRule rule =
            ExpireRule::fromString(QStringLiteral("any||60|%1").arg(flag));
        QVERIFY2(rule.keepPinned, qPrintable(flag));
    }
    // Everything else — including "false", "0" and typos — is false. A
    // misspelled flag therefore deletes pinned entries (see
    // keepPinnedFalseOverridesProtection).
    const QStringList falsy{QStringLiteral("0"), QStringLiteral("false"),
                            QStringLiteral("no"), QStringLiteral("off"),
                            QStringLiteral(""), QStringLiteral("flase"),
                            QStringLiteral("2")};
    for (const QString &flag : falsy) {
        const ExpireRule rule =
            ExpireRule::fromString(QStringLiteral("any||60|%1").arg(flag));
        QVERIFY2(!rule.keepPinned, qPrintable(flag));
    }
}

void TestExpire::rejectsExtraFieldsAndPipeInWildcard()
{
    QVERIFY(!ExpireRule::fromString(QStringLiteral("a|b|c|d|e")).isValid()); // 5 fields
    // A '|' inside the wildcard splits the codec: the rule is dropped.
    QVERIFY(!ExpireRule::fromString(QStringLiteral("text|konsole|work|60|1")).isValid());
}

void TestExpire::toStringMasksInvalidTypeAsAny()
{
    // Known quirk: contentTypeName(-2) prints "any", so an invalid rule
    // re-parses as a valid any-rule. Documented, not fixed: callers must
    // validate before serializing.
    ExpireRule rule;
    rule.contentType = -2;
    rule.ageSeconds = 60;
    QCOMPARE(rule.toString(), QStringLiteral("any||60|1"));
    const ExpireRule reparsed = ExpireRule::fromString(rule.toString());
    QCOMPARE(reparsed.contentType, -1);
    QVERIFY(reparsed.isValid());
}

void TestExpire::decodePreservesOrderAndDropsInvalid()
{
    const QStringList encoded{QStringLiteral("image||60|1"), QStringLiteral("text||120|1"),
                              QStringLiteral("any||180|1"), QStringLiteral("99||60|1")};
    const QList<ExpireRule> decoded = decodeRules(encoded);
    QCOMPARE(decoded.size(), 3);
    QCOMPARE(decoded.at(0).contentType, int(ContentType::Image));
    QCOMPARE(decoded.at(1).contentType, int(ContentType::Text));
    QCOMPARE(decoded.at(2).contentType, -1);
    QCOMPARE(decoded.at(2).ageSeconds, qint64(180));
}

void TestExpire::parsesAgeEdgeForms()
{
    QCOMPARE(ExpireRule::fromString(QStringLiteral("any|| 60 |1")).ageSeconds, qint64(60));
    const QStringList invalid{QStringLiteral("any||00|1"), QStringLiteral("any||+5|1"),
                              QStringLiteral("any||5.5|1"), QStringLiteral("any||0x10|1"),
                              QStringLiteral("any||1W|1"), QStringLiteral("any||  |1")};
    for (const QString &encoded : invalid)
        QVERIFY2(!ExpireRule::fromString(encoded).isValid(), qPrintable(encoded));
}

void TestExpire::cutoffBoundaryIsStrictlyOlder()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-edge-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 stamp = now - 3600 * 1000;
    storage.insertOrUpdate(makeRecord(QByteArrayLiteral("edge"), QStringLiteral("edge"), stamp));
    QCOMPARE(storage.stats().entryCount, qint64(1));

    // timestamp_ms < cutoff: exactly at the cutoff survives.
    QCOMPARE(storage.expireEntries(stamp, -1, QString(), true), 0);
    QCOMPARE(storage.stats().entryCount, qint64(1));
    // One millisecond later the same row is strictly older and goes.
    QCOMPARE(storage.expireEntries(stamp + 1, -1, QString(), true), 1);
    QCOMPARE(storage.stats().entryCount, qint64(0));
}

void TestExpire::questionMarkAndEscapedWildcards()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-wild-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto seedApp = [&](const char *hash, const QString &app) {
        ClipboardRecord record =
            makeRecord(QByteArray(hash), QStringLiteral("payload"), now - 7200 * 1000);
        record.sourceApp = app;
        storage.insertOrUpdate(record);
    };
    seedApp("w1", QStringLiteral("org.kde.konsole"));
    seedApp("w2", QStringLiteral("100%match"));
    seedApp("w3", QStringLiteral("my_app"));
    seedApp("w4", QStringLiteral("myXapp"));
    QCOMPARE(storage.stats().entryCount, qint64(4));

    // '?' matches exactly one character.
    QCOMPARE(storage.expireEntries(now, -1, QStringLiteral("org.kde.konsol?"), true), 1);
    // A literal '%' in the wildcard must not act as LIKE's match-all.
    QCOMPARE(storage.expireEntries(now, -1, QStringLiteral("100%match"), true), 1);
    QCOMPARE(storage.stats().entryCount, qint64(2));
    // A literal '_' matches only itself, not "any character".
    QCOMPARE(storage.expireEntries(now, -1, QStringLiteral("my_app"), true), 1);
    const auto rows = storage.fetchAll(FilterSpec{});
    QCOMPARE(rows.size(), 1);
    ClipboardRecord survivor;
    QVERIFY(storage.fetchFull(rows.first().id, &survivor));
    QCOMPARE(survivor.sourceApp, QStringLiteral("myXapp"));
}

void TestExpire::emptyWildcardMatchesNullSourceApp()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-nullapp-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // No source app recorded (NULL in the database).
    storage.insertOrUpdate(
        makeRecord(QByteArrayLiteral("noapp"), QStringLiteral("no app"), now - 7200 * 1000));
    QCOMPARE(storage.stats().entryCount, qint64(1));

    // "source_app LIKE 'konsole%'" never matches NULL.
    QCOMPARE(storage.expireEntries(now, -1, QStringLiteral("konsole*"), true), 0);
    QCOMPARE(storage.stats().entryCount, qint64(1));
    // An empty wildcard adds no predicate: the row is swept.
    QCOMPARE(storage.expireEntries(now, -1, QString(), true), 1);
    QCOMPARE(storage.stats().entryCount, qint64(0));
}

void TestExpire::multiRuleUnionKeepsBroadest()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-union-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);
    SettingsManager settings;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto seedApp = [&](const char *hash, const QString &text, const QString &app,
                             qint64 stamp, bool pinned) {
        ClipboardRecord record = makeRecord(QByteArray(hash), text, stamp);
        record.sourceApp = app;
        const qint64 id = storage.insertOrUpdate(record);
        if (pinned)
            storage.setPinned(id, true);
    };
    seedApp("u-konsole", QStringLiteral("old konsole"), QStringLiteral("org.kde.konsole"),
            now - 3 * 3600 * 1000, false);
    seedApp("u-kate", QStringLiteral("old kate"), QStringLiteral("org.kde.kate"),
            now - 3 * 3600 * 1000, false);
    seedApp("u-fresh", QStringLiteral("fresh konsole"), QStringLiteral("org.kde.konsole"), now,
            false);
    seedApp("u-pinned", QStringLiteral("old pinned"), QStringLiteral("org.kde.konsole"),
            now - 3 * 3600 * 1000, true);
    QCOMPARE(storage.stats().entryCount, qint64(4));

    // Narrow rule fires on konsole text only; the broad rule fires on both
    // old rows. Union: victims are swept once, pinned + fresh survive.
    ExpireRule narrow;
    narrow.contentType = int(ContentType::Text);
    narrow.sourceAppWildcard = QStringLiteral("org.kde.konsole*");
    narrow.ageSeconds = 2 * 3600;
    narrow.keepPinned = true;
    ExpireRule broad;
    broad.contentType = -1;
    broad.ageSeconds = 3600;
    broad.keepPinned = true;
    settings.setExpireRules({narrow, broad});

    ExpireScheduler scheduler(&storage, &settings);
    QSignalSpy spy(&scheduler, &ExpireScheduler::expired);
    scheduler.applyRules();

    QCOMPARE(spy.count(), 1); // one emission for the whole sweep
    QCOMPARE(spy.first().at(0).toInt(), 2);
    QCOMPARE(storage.stats().entryCount, qint64(2));
    const QList<qint64> victims = scheduler.takeLastExpiredIds();
    QCOMPARE(victims.size(), 2);
    QVERIFY(scheduler.takeLastExpiredIds().isEmpty()); // cleared on take
}

void TestExpire::keepPinnedFalseOverridesProtection()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-pin-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);
    SettingsManager settings;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    ClipboardRecord pinned =
        makeRecord(QByteArrayLiteral("pin"), QStringLiteral("pinned"), now - 7200 * 1000);
    const qint64 pinnedId = storage.insertOrUpdate(pinned);
    storage.setPinned(pinnedId, true);
    storage.insertOrUpdate(
        makeRecord(QByteArrayLiteral("plain"), QStringLiteral("plain"), now - 7200 * 1000));
    QCOMPARE(storage.stats().entryCount, qint64(2));

    // Parsed from strings to tie the parseBool danger end to end: "off" is
    // not truthy, so the second rule sweeps pinned rows the first one spared.
    const QList<ExpireRule> rules =
        decodeRules(QStringList{QStringLiteral("any||3600|1"), QStringLiteral("any||3600|off")});
    QCOMPARE(rules.size(), 2);
    QVERIFY(!rules.at(1).keepPinned);
    settings.setExpireRules(rules);

    ExpireScheduler scheduler(&storage, &settings);
    QSignalSpy spy(&scheduler, &ExpireScheduler::expired);
    scheduler.applyRules();

    // No first-wins / deny-wins: the union of both rules decides.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 2);
    QCOMPARE(storage.stats().entryCount, qint64(0));
}

void TestExpire::overlappingRulesCountOnce()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-overlap-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);
    SettingsManager settings;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    storage.insertOrUpdate(
        makeRecord(QByteArrayLiteral("v1"), QStringLiteral("victim 1"), now - 7200 * 1000));
    storage.insertOrUpdate(
        makeRecord(QByteArrayLiteral("v2"), QStringLiteral("victim 2"), now - 7200 * 1000));

    ExpireRule rule;
    rule.contentType = -1;
    rule.ageSeconds = 3600;
    rule.keepPinned = true;
    settings.setExpireRules({rule, rule}); // same rule twice

    ExpireScheduler scheduler(&storage, &settings);
    QSignalSpy spy(&scheduler, &ExpireScheduler::expired);
    scheduler.applyRules();

    // The second pass finds nothing (rows already in trash): counted once.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 2);
    QCOMPARE(scheduler.takeLastExpiredIds().size(), 2);
    QCOMPARE(storage.stats().entryCount, qint64(0));
}

void TestExpire::noSignalWithoutVictims()
{
    const QString path = m_dir.filePath(
        QStringLiteral("expire-quiet-%1.db").arg(QRandomGenerator::global()->generate64()));
    StorageManager storage(path);
    SettingsManager settings;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    storage.insertOrUpdate(makeRecord(QByteArrayLiteral("fresh"), QStringLiteral("fresh"), now));

    ExpireRule rule;
    rule.contentType = -1;
    rule.ageSeconds = 3600;
    settings.setExpireRules({rule});

    ExpireScheduler scheduler(&storage, &settings);
    QSignalSpy spy(&scheduler, &ExpireScheduler::expired);
    scheduler.applyRules();

    QCOMPARE(spy.count(), 0); // valid rule, nothing aged out: silent
    QVERIFY(scheduler.takeLastExpiredIds().isEmpty());
    QCOMPARE(storage.stats().entryCount, qint64(1));
}

QTEST_GUILESS_MAIN(TestExpire)
#include "tst_expire.moc"