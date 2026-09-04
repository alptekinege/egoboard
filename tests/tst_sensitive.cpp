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
    void detectsAllPrivateKeyTypes();
    void detectsAllGitHubTokenTypes();
    void detectsAllSlackTokenTypes();
    void detectsAwsKeysAndBoundaries();
    void detectsJwtAndBearerVariations();
    void multiSecretFullSanitization();
    void reportsFindingSpans();
    void ignoresUnknownRedactionKinds();
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

void TestSensitive::detectsAllPrivateKeyTypes()
{
    const QStringList headers = {
        QStringLiteral("-----BEGIN PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN RSA PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN EC PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN DSA PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN OPENSSH PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN PGP PRIVATE KEY-----")
    };

    for (const QString &header : headers) {
        const QString fullText = QStringLiteral("key data:\n%1\nMIIE...\n-----END-----").arg(header);
        QVERIFY2(SensitiveDataDetector::isSensitive(fullText), qPrintable(header));
        QVERIFY(SensitiveDataDetector::kinds(fullText).contains(QStringLiteral("private-key")));

        const auto redacted = SensitiveDataDetector::redact(fullText, {QStringLiteral("private-key")});
        QVERIFY(!redacted.text.contains(header));
        QVERIFY(redacted.text.contains(QStringLiteral("••••")));
    }
}

void TestSensitive::detectsAllGitHubTokenTypes()
{
    const QStringList prefixes = {
        QStringLiteral("ghp_"), // personal access token
        QStringLiteral("gho_"), // oauth token
        QStringLiteral("ghu_"), // user-to-server token
        QStringLiteral("ghs_"), // server-to-server token
        QStringLiteral("ghr_")  // refresh token
    };

    for (const QString &pfx : prefixes) {
        const QString token = pfx + QStringLiteral("1234567890abcdefghijklmnopqrstuvwxyz");
        const QString text = QStringLiteral("GITHUB_TOKEN=%1").arg(token);
        QVERIFY2(SensitiveDataDetector::isSensitive(text), qPrintable(token));
        QVERIFY(SensitiveDataDetector::kinds(text).contains(QStringLiteral("github-token")));
    }

    // Too short (less than 20 chars after prefix)
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("ghp_short12345")).contains(QStringLiteral("github-token")));
}

void TestSensitive::detectsAllSlackTokenTypes()
{
    const QStringList prefixes = {
        QStringLiteral("xoxb-"), // bot token
        QStringLiteral("xoxa-"), // app token
        QStringLiteral("xoxp-"), // user token
        QStringLiteral("xoxr-"), // refresh token
        QStringLiteral("xoxs-")  // session token
    };

    for (const QString &pfx : prefixes) {
        const QString token = pfx + QStringLiteral("123456789012-345678901234-abcdefghijklmnopqrstuv");
        const QString text = QStringLiteral("SLACK=%1").arg(token);
        QVERIFY2(SensitiveDataDetector::isSensitive(text), qPrintable(token));
        QVERIFY(SensitiveDataDetector::kinds(text).contains(QStringLiteral("slack-token")));
    }
}

void TestSensitive::detectsAwsKeysAndBoundaries()
{
    // Valid 16-character alphanumeric suffix after AKIA
    const QString validKey = QStringLiteral("AKIAIOSFODNN7EXAMPLE");
    QVERIFY(SensitiveDataDetector::kinds(validKey).contains(QStringLiteral("aws-key")));

    // 15 characters: invalid
    const QString shortKey = QStringLiteral("AKIAIOSFODNN7EXAMP");
    QVERIFY(!SensitiveDataDetector::kinds(shortKey).contains(QStringLiteral("aws-key")));

    // Wrong prefix: invalid
    const QString wrongPrefix = QStringLiteral("BKIAIOSFODNN7EXAMPLE");
    QVERIFY(!SensitiveDataDetector::kinds(wrongPrefix).contains(QStringLiteral("aws-key")));

    // Special characters in suffix: invalid
    const QString punctKey = QStringLiteral("AKIAIOSFODNN7EXAM!");
    QVERIFY(!SensitiveDataDetector::kinds(punctKey).contains(QStringLiteral("aws-key")));
}

void TestSensitive::detectsJwtAndBearerVariations()
{
    const QString jwt = QStringLiteral("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIn0.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c");
    QVERIFY(SensitiveDataDetector::kinds(jwt).contains(QStringLiteral("jwt")));

    const QString bearer = QStringLiteral("Authorization: Bearer mySecretTokenValue12345678==");
    QVERIFY(SensitiveDataDetector::kinds(bearer).contains(QStringLiteral("bearer-token")));

    // Redacting Bearer token replaces it with bullets
    const auto res = SensitiveDataDetector::redact(bearer, {QStringLiteral("bearer-token")});
    QVERIFY(!res.text.contains(QStringLiteral("mySecretTokenValue12345678==")));
    QVERIFY(res.text.contains(QStringLiteral("Authorization: ••••")));
}

void TestSensitive::multiSecretFullSanitization()
{
    const QString source = QStringLiteral(
        "Config:\n"
        "AWS_KEY=AKIAIOSFODNN7EXAMPLE\n"
        "GH_TOKEN=ghp_1234567890abcdefghijklmnopqrstuvwxyz\n"
        "SLACK=xoxb-1234567890-abcdefghijklmnop\n"
        "CARD=4111 1111 1111 1111\n"
        "OPENAI=sk-1234567890abcdef1234567890abcdef\n"
        "PASS: password=superSecretPassword123\n"
    );

    QVERIFY(SensitiveDataDetector::isSensitive(source));
    const QStringList kinds = SensitiveDataDetector::kinds(source);
    QVERIFY(kinds.contains(QStringLiteral("aws-key")));
    QVERIFY(kinds.contains(QStringLiteral("github-token")));
    QVERIFY(kinds.contains(QStringLiteral("slack-token")));
    QVERIFY(kinds.contains(QStringLiteral("creditcard")));
    QVERIFY(kinds.contains(QStringLiteral("api-key")));
    QVERIFY(kinds.contains(QStringLiteral("credential")));

    const auto result = SensitiveDataDetector::redact(source);
    QVERIFY(!result.text.contains(QStringLiteral("AKIAIOSFODNN7EXAMPLE")));
    QVERIFY(!result.text.contains(QStringLiteral("ghp_1234567890")));
    QVERIFY(!result.text.contains(QStringLiteral("xoxb-1234567890")));
    QVERIFY(!result.text.contains(QStringLiteral("4111 1111 1111 1111")));
    QVERIFY(!result.text.contains(QStringLiteral("sk-1234567890")));
    QVERIFY(!result.text.contains(QStringLiteral("superSecretPassword123")));

    // Labels/keys should remain intact
    QVERIFY(result.text.contains(QStringLiteral("AWS_KEY=")));
    QVERIFY(result.text.contains(QStringLiteral("GH_TOKEN=")));
    QVERIFY(result.text.contains(QStringLiteral("SLACK=")));
    QVERIFY(result.text.contains(QStringLiteral("CARD=")));
    QVERIFY(result.text.contains(QStringLiteral("OPENAI=")));
}

void TestSensitive::reportsFindingSpans()
{
    const QString text = QStringLiteral("prefix password=hunter2 suffix");
    const auto findings = SensitiveDataDetector::scan(text);
    QCOMPARE(findings.size(), 1);
    QCOMPARE(findings.first().kind, QStringLiteral("credential"));
    QCOMPARE(findings.first().offset, text.indexOf(QStringLiteral("password=hunter2")));
    QCOMPARE(findings.first().length, QStringLiteral("password=hunter2").size());
}

void TestSensitive::ignoresUnknownRedactionKinds()
{
    const QString input = QStringLiteral("password=hunter2 and 4111 1111 1111 1111");
    const auto result = SensitiveDataDetector::redact(input, {QStringLiteral("not-a-kind")});
    QCOMPARE(result.text, input);
    QCOMPARE(result.redactedCount, 0);
    QVERIFY(result.redactedKinds.isEmpty());

    const auto credentialOnly = SensitiveDataDetector::redact(
        input, {QStringLiteral("credential"), QStringLiteral("credential")});
    QCOMPARE(credentialOnly.redactedCount, 1);
    QCOMPARE(credentialOnly.redactedKinds, QStringList{QStringLiteral("credential")});
    QVERIFY(credentialOnly.text.contains(QStringLiteral("4111 1111 1111 1111")));
}

QTEST_GUILESS_MAIN(TestSensitive)
#include "tst_sensitive.moc"
