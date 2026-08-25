#include <QtTest>

#include "SensitiveDataDetector.h"

class TestSensitive : public QObject
{
    Q_OBJECT

private slots:
    void detectsCreditCards();
    void rejectsInvalidCardNumbers();
    void detectsCredentials();
    void detectsTokens();
    void detectsMultipleFindings();
    void detectsAdditionalCardFormats();
    void ignoresNormalText();
    void redactsSecrets();
    void redactsPerKind();
    void redactsNothingWhenDisabled();
    void redactKeepsNonSensitiveText();
    void redactMergesOverlappingSpans();
};

void TestSensitive::detectsCreditCards()
{
    // Classic Visa test number (Luhn-valid).
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("Card: 4111 1111 1111 1111")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("4111111111111111")));
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("4111-1111-1111-1111"))
                .contains(QStringLiteral("creditcard")));
}

void TestSensitive::rejectsInvalidCardNumbers()
{
    // Same digit count but Luhn-invalid.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("4111 1111 1111 1112")));
    // Too short.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("4111 1111")));
}

void TestSensitive::detectsCredentials()
{
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("password=hunter2")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("SECRET_TOKEN: abc123")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("api_key = Zx9pQ2mT...")));
}

void TestSensitive::detectsTokens()
{
    QVERIFY(SensitiveDataDetector::isSensitive(
        QStringLiteral("export GITHUB_TOKEN=ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ123456")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("aws AKIAIOSFODNN7EXAMPLE")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N65LhO3ZQ")));
    QVERIFY(SensitiveDataDetector::isSensitive(
        QStringLiteral("-----BEGIN RSA PRIVATE KEY-----")));
}

void TestSensitive::detectsMultipleFindings()
{
    const QString text = QStringLiteral(
        "password=hunter2, key=AKIAIOSFODNN7EXAMPLE, card=5555 5555 5555 4444");
    const auto findings = SensitiveDataDetector::scan(text);
    QVERIFY(findings.size() >= 3);

    const QStringList kinds = SensitiveDataDetector::kinds(text);
    QVERIFY(kinds.contains(QStringLiteral("credential")));
    QVERIFY(kinds.contains(QStringLiteral("aws-key")));
    QVERIFY(kinds.contains(QStringLiteral("creditcard")));
}

void TestSensitive::detectsAdditionalCardFormats()
{
    // Known Luhn-valid Mastercard and American Express test numbers.
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("5555555555554444")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("378282246310005")));
}

void TestSensitive::ignoresNormalText()
{
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("The quick brown fox jumps")));
    QVERIFY(!SensitiveDataDetector::isSensitive(
        QStringLiteral("Meeting at 14:00 in room 42, agenda: Q3 planning")));
    QVERIFY(!SensitiveDataDetector::isSensitive(QString()));
}

void TestSensitive::redactsSecrets()
{
    const auto result = SensitiveDataDetector::redact(
        QStringLiteral("Card: 4111 1111 1111 1111 password=hunter2"));
    QVERIFY(!result.text.contains(QStringLiteral("4111")));
    QVERIFY(!result.text.contains(QStringLiteral("hunter2")));
    QVERIFY(result.text.contains(QStringLiteral("Card:")));
    QCOMPARE(result.redactedCount, 2);
    QVERIFY(result.redactedKinds.contains(QStringLiteral("creditcard")));
    QVERIFY(result.redactedKinds.contains(QStringLiteral("credential")));
}

void TestSensitive::redactsPerKind()
{
    // Redact credentials only: the card number survives, the password does not.
    const auto result = SensitiveDataDetector::redact(
        QStringLiteral("Card: 4111 1111 1111 1111 password=hunter2"),
        QStringList{QStringLiteral("credential")});
    QVERIFY(result.text.contains(QStringLiteral("4111 1111 1111 1111")));
    QVERIFY(!result.text.contains(QStringLiteral("hunter2")));
    QCOMPARE(result.redactedCount, 1);
    QCOMPARE(result.redactedKinds, QStringList{QStringLiteral("credential")});
}

void TestSensitive::redactsNothingWhenDisabled()
{
    const QString input = QStringLiteral("no secrets here, just 42");
    const auto result = SensitiveDataDetector::redact(input);
    QCOMPARE(result.text, input);
    QCOMPARE(result.redactedCount, 0);
    QVERIFY(result.redactedKinds.isEmpty());
}

void TestSensitive::redactKeepsNonSensitiveText()
{
    const auto result = SensitiveDataDetector::redact(
        QStringLiteral("token sk-ABCDEFGHIJKLMNOP and notes after"),
        QStringList{QStringLiteral("creditcard")}); // wrong kind ⇒ token survives
    QVERIFY(result.text.contains(QStringLiteral("sk-A")));
    QVERIFY(result.text.contains(QStringLiteral("and notes after")));

    // allKinds exposes every toggleable kind
    const QStringList all = SensitiveDataDetector::allKinds();
    QVERIFY(all.contains(QStringLiteral("creditcard")));
    QVERIFY(all.contains(QStringLiteral("private-key")));
    QVERIFY(all.size() >= 5);

    // Empty enabledKinds redacts everything, including multi-token strings.
    const auto wide = SensitiveDataDetector::redact(
        QStringLiteral("ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ123456 eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N65LhO3ZQ"));
    QCOMPARE(wide.redactedCount, 2);
}

void TestSensitive::redactMergesOverlappingSpans()
{
    // "Bearer sk-…" matches both bearer-token and api-key. The ranges
    // overlap; redaction must merge them so surrounding text survives intact.
    const auto result = SensitiveDataDetector::redact(
        QStringLiteral("Bearer sk-ABCDEFGHIJKLMNOPQRSTUVWXYZ ninja"));
    QCOMPARE(result.text, QStringLiteral("•••• ninja"));
    QCOMPARE(result.redactedCount, 1);
    QVERIFY(result.redactedKinds.contains(QStringLiteral("bearer-token")));
    QVERIFY(result.redactedKinds.contains(QStringLiteral("api-key")));

    // Surrounding text is preserved verbatim.
    const auto wide = SensitiveDataDetector::redact(
        QStringLiteral("before -----BEGIN RSA PRIVATE KEY----- after"));
    QCOMPARE(wide.text, QStringLiteral("before •••• after"));
    QCOMPARE(wide.redactedCount, 1);
}

QTEST_GUILESS_MAIN(TestSensitive)
#include "tst_sensitive.moc"
