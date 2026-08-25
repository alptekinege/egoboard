#include <QtTest>

#include "ExpirePolicy.h"
#include "StorageManager.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTemporaryDir>

// Rule codec + end-to-end expiry against a real temp database.
class TestExpire : public QObject
{
    Q_OBJECT

private slots:
    void roundTripThroughListCodec();
    void parsesHumanAges();
    void rejectsMalformedRules();
    void appliesRuleAgainstStorage();

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

QTEST_GUILESS_MAIN(TestExpire)
#include "tst_expire.moc"