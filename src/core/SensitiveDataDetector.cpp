#include "SensitiveDataDetector.h"

#include <QChar>
#include <QList>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

#include <algorithm>

namespace {

struct Rule {
    const char *pattern;
    const char *kind;
    bool luhn; // validate candidate as a credit card number
};

// Ordered from most to least specific. All patterns are case-insensitive.
const Rule kRules[] = {
    {"-----BEGIN (RSA |EC |DSA |OPENSSH |PGP |)?PRIVATE KEY-----", "private-key", false},
    {"AKIA[0-9A-Z]{16}", "aws-key", false},
    {"gh[pousr]_[A-Za-z0-9]{20,}", "github-token", false},
    {"sk-[A-Za-z0-9]{16,}", "api-key", false},
    {"xox[baprs]-[A-Za-z0-9-]{10,}", "slack-token", false},
    {"eyJ[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{5,}", "jwt", false},
    {"Bearer\\s+[A-Za-z0-9._~+/-]{16,}={0,2}", "bearer-token", false},
    {"\\b(?:\\d[ -]?){13,19}\\b", "creditcard", true},
    {"\\b(?:password|passwd|pwd|secret|token|api[_-]?key|apikey|access[_-]?key|"
     "authorization)[\\w-]*\\s*[:=]\\s*\\S+",
     "credential", false},
};

} // namespace

bool SensitiveDataDetector::luhnValid(const QString &digits)
{
    if (digits.size() < 13)
        return false;
    int sum = 0;
    bool doubleDigit = false;
    int digitCount = 0;
    for (int i = digits.size() - 1; i >= 0; --i) {
        const QChar c = digits.at(i);
        if (!c.isDigit())
            continue; // skip separators (spaces/dashes)
        ++digitCount;
        int digit = c.digitValue();
        if (doubleDigit) {
            digit *= 2;
            if (digit > 9)
                digit -= 9;
        }
        sum += digit;
        doubleDigit = !doubleDigit;
    }
    return digitCount >= 13 && digitCount <= 19 && (sum % 10) == 0;
}

QList<SensitiveDataDetector::Finding> SensitiveDataDetector::scan(const QString &text)
{
    QList<Finding> findings;
    if (text.isEmpty())
        return findings;

    for (const Rule &rule : kRules) {
        QRegularExpression re(QString::fromLatin1(rule.pattern),
                              QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator it = re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (rule.luhn && !luhnValid(match.captured(0)))
                continue;
            findings.append(Finding{static_cast<int>(match.capturedStart(0)),
                                    static_cast<int>(match.capturedLength(0)),
                                    QString::fromLatin1(rule.kind)});
        }
    }
    return findings;
}

bool SensitiveDataDetector::isSensitive(const QString &text)
{
    return !scan(text).isEmpty();
}

QStringList SensitiveDataDetector::kinds(const QString &text)
{
    QStringList result;
    const auto findings = scan(text);
    for (const Finding &finding : findings) {
        if (!result.contains(finding.kind))
            result.append(finding.kind);
    }
    return result;
}

QStringList SensitiveDataDetector::allKinds()
{
    QStringList result;
    for (const Rule &rule : kRules) {
        const QString kind = QString::fromLatin1(rule.kind);
        if (!result.contains(kind))
            result.append(kind);
    }
    return result;
}

SensitiveDataDetector::RedactionResult SensitiveDataDetector::redact(
    const QString &text, const QStringList &enabledKinds)
{
    RedactionResult result;
    result.text = text;
    if (text.isEmpty())
        return result;

    // Keep only enabled kinds, then merge overlapping matches into maximal
    // spans so replacement offsets never go stale (e.g. "Bearer sk-…" hits
    // both bearer-token and api-key).
    const bool allEnabled = enabledKinds.isEmpty();
    QList<Finding> spans;
    {
        QList<Finding> findings;
        const auto all = scan(text);
        for (const Finding &finding : all)
            if (allEnabled || enabledKinds.contains(finding.kind))
                findings.append(finding);
        std::sort(findings.begin(), findings.end(),
                  [](const Finding &a, const Finding &b) {
                      return a.offset < b.offset
                          || (a.offset == b.offset && a.length > b.length);
                  });
        for (const Finding &finding : findings) {
            if (!spans.isEmpty() && finding.offset < spans.last().offset + spans.last().length) {
                const int end = qMax(spans.last().offset + spans.last().length,
                                     finding.offset + finding.length);
                spans.last().length = end - spans.last().offset;
                if (!spans.last().kind.contains(finding.kind)) // may be comma-joined later
                    spans.last().kind = spans.last().kind + QLatin1Char(',') + finding.kind;
            } else {
                spans.append(finding);
            }
        }
    }

    // Replace from the end so earlier offsets stay valid.
    for (auto it = spans.crbegin(); it != spans.crend(); ++it) {
        result.text.replace(it->offset, it->length, QStringLiteral("••••"));
        ++result.redactedCount;
        const QStringList kinds = it->kind.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &kind : kinds)
            if (!result.redactedKinds.contains(kind))
                result.redactedKinds.append(kind);
    }
    return result;
}
