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
    void luhnBoundaryLengths();
    void luhnSeparatorMatrix();
    void creditCardFalsePositives();
    void creditCardFindingSpans();
    void cardWordBoundaries();
    void awsKeyEdgeCases();
    void githubTokenNegatives();
    void slackTokenNegatives();
    void jwtBearerEdgeCases();
    void apiKeyNegatives();
    void credentialVariants();
    void privateKeyCaseAndNegatives();
    void allKindsExactOrder();
    void kindsRuleOrderAndDedup();
    void badInputs();
    void redactUnknownPlusValidMix();
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

void TestSensitive::luhnBoundaryLengths()
{
    // Shortest accepted length (13 digits, Visa test number).
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("4222222222222")));
    // Longest accepted length (19 digits).
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("4111111111111111110")));
    // 12 digits can never satisfy {13,19}: rejected without consulting Luhn.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("411111111111")));
    // 20 digits: no 13-19 digit window has a word boundary on both sides.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("41111111111111111111")));
}

void TestSensitive::luhnSeparatorMatrix()
{
    // Mixed separators are skipped by luhnValid and accepted.
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("4111-1111 1111-1111")));
    // Trailing separator: the regex backtracks it, the match stays Luhn-valid.
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("4111 1111 1111 1111 ")));
    // Double space breaks digit continuity: no 13-digit run remains.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("4111  1111  1111  1111")));
    // Dots are not separators: isolated 4-digit groups never reach 13.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("4111.1111.1111.1111")));
}

void TestSensitive::creditCardFalsePositives()
{
    // Sequential digits are Luhn-invalid.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("1234 5678 9012 3456")));
    // Phone-number digit runs stay below 13.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("Call me at +1-555-123-4567")));
    // ISBN-13 check differs from Luhn: this one is Luhn-invalid.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("ISBN 978-3-16-148410-0")));
    // All-zeros is Luhn-valid (sum == 0), so it is flagged. Documents the
    // current fail-closed behavior for uniform-digit runs.
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("0000 0000 0000 0000")));
}

void TestSensitive::creditCardFindingSpans()
{
    const QString text = QStringLiteral("card 4111-1111-1111-1111 end");
    const auto findings = SensitiveDataDetector::scan(text);
    QCOMPARE(findings.size(), 1);
    QCOMPARE(findings.first().kind, QStringLiteral("creditcard"));
    QCOMPARE(findings.first().offset, text.indexOf(QStringLiteral("4111-1111-1111-1111")));
    // Greedy [ -]? consumes the trailing space, so the span is 20 chars.
    // Documents current behavior: redaction eats one trailing separator.
    QCOMPARE(findings.first().length, 20);
    QCOMPARE(text.mid(findings.first().offset, findings.first().length),
             QStringLiteral("4111-1111-1111-1111 "));
}

void TestSensitive::cardWordBoundaries()
{
    // Embedded in word characters: no \b on either side, no match.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("abc4111111111111111def")));
    // Bare number surrounded by whitespace is fine.
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("  4111111111111111  ")));
}

void TestSensitive::awsKeyEdgeCases()
{
    // Patterns are CaseInsensitive: lowercase prefix still matches.
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("akiaiosfodnn7example"))
                .contains(QStringLiteral("aws-key")));
    // No \b in the pattern: embedded keys match as substrings. Documents the
    // current prefix-match behavior.
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("XAKIAIOSFODNN7EXAMPLEX"))
                .contains(QStringLiteral("aws-key")));
    // Overlong suffix: the first 20 chars match.
    const QString overlong = QStringLiteral("AKIAIOSFODNN7EXAMPLEX");
    const auto findings = SensitiveDataDetector::scan(overlong);
    QCOMPARE(findings.size(), 1);
    QCOMPARE(findings.first().kind, QStringLiteral("aws-key"));
    QCOMPARE(findings.first().length, 20);
}

void TestSensitive::githubTokenNegatives()
{
    // Prefix letters outside [pousr] are rejected.
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("ghq_1234567890abcdefghij"))
                 .contains(QStringLiteral("github-token")));
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("ghc_1234567890abcdefghij"))
                 .contains(QStringLiteral("github-token")));
    // Case-insensitive: uppercase prefix matches.
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("GHP_1234567890abcdefghij"))
                .contains(QStringLiteral("github-token")));
    // '-' is not in the suffix class: the match stops before it (4 + 20 chars).
    const QString dashed = QStringLiteral("ghp_12345678901234567890-abc");
    const auto findings = SensitiveDataDetector::scan(dashed);
    QVERIFY(!findings.isEmpty());
    QCOMPARE(findings.first().kind, QStringLiteral("github-token"));
    QCOMPARE(findings.first().length, 24);
    // Oversized suffixes still match (no upper bound).
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("ghp_") + QString(100, u'a'))
                .contains(QStringLiteral("github-token")));
}

void TestSensitive::slackTokenNegatives()
{
    // Prefix letters outside [baprs] are rejected.
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("xoxc-1234567890123456"))
                 .contains(QStringLiteral("slack-token")));
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("xoxo-1234567890123456"))
                 .contains(QStringLiteral("slack-token")));
    // Suffix shorter than 10 chars is rejected.
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("xoxb-short"))
                 .contains(QStringLiteral("slack-token")));
}

void TestSensitive::jwtBearerEdgeCases()
{
    // Two segments (missing signature) are rejected.
    QVERIFY(!SensitiveDataDetector::kinds(
                    QStringLiteral("eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0"))
                 .contains(QStringLiteral("jwt")));
    // A Bearer JWT is double-counted: the token body matches both rules.
    const QString bearerJwt = QStringLiteral(
        "Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N65LhO3ZQ");
    const QStringList both = SensitiveDataDetector::kinds(bearerJwt);
    QVERIFY(both.contains(QStringLiteral("jwt")));
    QVERIFY(both.contains(QStringLiteral("bearer-token")));
    // Case-insensitive scheme.
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("bearer mySecretTokenValue12345678=="))
                .contains(QStringLiteral("bearer-token")));
    // Tab counts as \s between scheme and token.
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("Bearer\tmySecretTokenValue12345678"))
                .contains(QStringLiteral("bearer-token")));
    // Token shorter than 16 chars is rejected; bare scheme has no token.
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("Bearer short"))
                 .contains(QStringLiteral("bearer-token")));
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("Bearer"))
                 .contains(QStringLiteral("bearer-token")));
}

void TestSensitive::apiKeyNegatives()
{
    // Suffix shorter than 16 chars is rejected.
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("sk-short"))
                 .contains(QStringLiteral("api-key")));
    // Case-insensitive prefix.
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("SK-1234567890abcdef"))
                .contains(QStringLiteral("api-key")));
    // Underscore is not the documented separator.
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("sk_1234567890abcdef"))
                 .contains(QStringLiteral("api-key")));
}

void TestSensitive::credentialVariants()
{
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("passwd=secret")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("pwd: hunter2")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("apikey=XYZ123")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("access-key = hunter2")));
    QVERIFY(SensitiveDataDetector::isSensitive(QStringLiteral("authorization=Basic abc123")));
    // No value after the separator: \S+ is mandatory.
    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("password=")));
    // Value stops at whitespace: the finding covers "password=hello" only.
    const QString text = QStringLiteral("password=hello world");
    const auto findings = SensitiveDataDetector::scan(text);
    QCOMPARE(findings.size(), 1);
    QCOMPARE(findings.first().kind, QStringLiteral("credential"));
    QCOMPARE(findings.first().length, QStringLiteral("password=hello").size());
}

void TestSensitive::privateKeyCaseAndNegatives()
{
    // Case-insensitive header.
    QVERIFY(SensitiveDataDetector::kinds(QStringLiteral("-----begin rsa private key-----"))
                .contains(QStringLiteral("private-key")));
    // A public key is not a private key.
    QVERIFY(!SensitiveDataDetector::kinds(QStringLiteral("-----BEGIN PUBLIC KEY-----"))
                 .contains(QStringLiteral("private-key")));
}

void TestSensitive::allKindsExactOrder()
{
    const QStringList expected{QStringLiteral("private-key"), QStringLiteral("aws-key"),
                               QStringLiteral("github-token"), QStringLiteral("api-key"),
                               QStringLiteral("slack-token"), QStringLiteral("jwt"),
                               QStringLiteral("bearer-token"), QStringLiteral("creditcard"),
                               QStringLiteral("credential")};
    QCOMPARE(SensitiveDataDetector::allKinds(), expected);
}

void TestSensitive::kindsRuleOrderAndDedup()
{
    // Findings follow rule order (creditcard before credential), not text order,
    // and each kind is reported once.
    const QString text = QStringLiteral(
        "password=hunter2 card 4111 1111 1111 1111 password=abc");
    const QStringList expected{QStringLiteral("creditcard"), QStringLiteral("credential")};
    QCOMPARE(SensitiveDataDetector::kinds(text), expected);
}

void TestSensitive::badInputs()
{
    QVERIFY(SensitiveDataDetector::kinds(QString()).isEmpty());
    QCOMPARE(SensitiveDataDetector::scan(QString()).size(), 0);
    const auto empty = SensitiveDataDetector::redact(QString());
    QCOMPARE(empty.text, QString());
    QCOMPARE(empty.redactedCount, 0);

    QVERIFY(!SensitiveDataDetector::isSensitive(QStringLiteral("   \n\t  ")));

    // Embedded null byte breaks the match (no crash): "password\0=abc" no
    // longer satisfies keyword[:=]value, so the scanner stays negative.
    QString nulled = QStringLiteral("password=abc");
    nulled.insert(8, QChar(u'\0'));
    QVERIFY(!SensitiveDataDetector::isSensitive(nulled));

    // Lone surrogate: invalid UTF-16 must not crash the scanner.
    QVERIFY(!SensitiveDataDetector::isSensitive(QString(QChar(0xD800))));

    // \d is ASCII-only (no UCP): non-Latin digits never form a card number.
    QVERIFY(!SensitiveDataDetector::isSensitive(
        QStringLiteral("٤١١١١١١١١١١١١١١١١")));

    // Oversized inputs complete (regex backtracking guard) and stay negative.
    QVERIFY(!SensitiveDataDetector::isSensitive(QString(1000000, u'a')));
    QVERIFY(!SensitiveDataDetector::isSensitive(
        QStringLiteral("password") + QString(10000, u'a')));
}

void TestSensitive::redactUnknownPlusValidMix()
{
    const QString input = QStringLiteral("password=hunter2 and 4111 1111 1111 1111");
    const auto result = SensitiveDataDetector::redact(
        input, {QStringLiteral("not-a-kind"), QStringLiteral("credential")});
    QVERIFY(!result.text.contains(QStringLiteral("hunter2")));
    QVERIFY(result.text.contains(QStringLiteral("4111 1111 1111 1111")));
    QCOMPARE(result.redactedCount, 1);
    QCOMPARE(result.redactedKinds, QStringList{QStringLiteral("credential")});
}

QTEST_GUILESS_MAIN(TestSensitive)
#include "tst_sensitive.moc"
