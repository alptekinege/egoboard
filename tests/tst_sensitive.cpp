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

QTEST_GUILESS_MAIN(TestSensitive)
#include "tst_sensitive.moc"
