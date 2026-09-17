#include "ExpirePolicy.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace {

// Content type field: "any" or a content-type tag (or a numeric enum value,
// for hand-written configs). Unknown names map to -1 (any); a numeric value
// outside the enum is invalid (kInvalidContentType) and the rule is dropped,
// rather than silently matching nothing or everything.
constexpr int kInvalidContentType = -2;

int parseContentType(const QString &field)
{
    const QString trimmed = field.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("any"), Qt::CaseInsensitive) == 0)
        return -1;
    bool numeric = false;
    const int value = trimmed.toInt(&numeric);
    if (numeric)
        return (value >= 0 && value <= static_cast<int>(ContentType::Files)) ? value
                                                                             : kInvalidContentType;
    for (int type = 0; type <= static_cast<int>(ContentType::Files); ++type) {
        if (QString::fromLatin1(contentTypeTag(static_cast<ContentType>(type)))
                .compare(trimmed, Qt::CaseInsensitive) == 0)
            return type;
    }
    return -1;
}

QString contentTypeName(int type)
{
    if (type >= 0 && type <= static_cast<int>(ContentType::Files))
        return QString::fromLatin1(contentTypeTag(static_cast<ContentType>(type)));
    return QStringLiteral("any");
}

// "86400", "24h", "90m", "30s" → seconds. Non-positive or unparseable → 0.
qint64 parseAgeSeconds(const QString &field)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(\d+)\s*([smhd]?)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(field.trimmed());
    if (!match.hasMatch())
        return 0;
    const qint64 amount = match.captured(1).toLongLong();
    const QString unit = match.captured(2).toLower();
    const qint64 multiplier = unit == QStringLiteral("s") ? 1
                             : unit == QStringLiteral("m") ? 60
                             : unit == QStringLiteral("h") ? 3600
                             : unit == QStringLiteral("d") ? 86400
                                                           : 1;
    return amount * multiplier;
}

bool parseBool(const QString &field)
{
    const QString trimmed = field.trimmed().toLower();
    return trimmed == QStringLiteral("1") || trimmed == QStringLiteral("true")
        || trimmed == QStringLiteral("yes") || trimmed == QStringLiteral("on");
}

} // namespace

QString ExpireRule::toString() const
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(contentTypeName(contentType), sourceAppWildcard)
        .arg(ageSeconds)
        .arg(keepPinned ? 1 : 0);
}

ExpireRule ExpireRule::fromString(const QString &encoded)
{
    ExpireRule rule;
    const QStringList parts = encoded.split(QLatin1Char('|'));
    if (parts.size() != 4)
        return rule;
    rule.contentType = parseContentType(parts.at(0));
    rule.sourceAppWildcard = parts.at(1);
    rule.ageSeconds = parseAgeSeconds(parts.at(2));
    rule.keepPinned = parseBool(parts.at(3));
    return rule;
}

QStringList encodeRules(const QList<ExpireRule> &rules)
{
    QStringList encoded;
    encoded.reserve(rules.size());
    for (const ExpireRule &rule : rules)
        encoded.append(rule.toString());
    return encoded;
}

QList<ExpireRule> decodeRules(const QStringList &encoded)
{
    QList<ExpireRule> rules;
    rules.reserve(encoded.size());
    for (const QString &item : encoded) {
        const ExpireRule rule = ExpireRule::fromString(item);
        if (rule.isValid())
            rules.append(rule);
    }
    return rules;
}