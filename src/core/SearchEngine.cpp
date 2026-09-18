#include "SearchEngine.h"

#include "ContentType.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>

#include <optional>

namespace {

constexpr int kMaxTokenChars = 64;

bool tableExists(const QSqlDatabase &db, const QString &name)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=:n LIMIT 1"));
    q.bindValue(QStringLiteral(":n"), name);
    if (!q.exec() || !q.next())
        return false;
    return true;
}

// Whitespace tokenizer that keeps quoted runs together, so `app:"My App"` and
// `"one two"` arrive as single tokens. An unmatched quote stays literal.
QStringList tokenizeWithQuotes(const QString &input)
{
    QStringList tokens;
    const int n = input.size();
    int i = 0;
    while (i < n) {
        while (i < n && input.at(i).isSpace())
            ++i;
        if (i >= n)
            break;
        QString token;
        while (i < n) {
            const QChar c = input.at(i);
            if (c == QLatin1Char('"')) {
                const int close = input.indexOf(QLatin1Char('"'), i + 1);
                if (close < 0) {
                    token += c; // unmatched: keep as a literal character
                    ++i;
                    continue;
                }
                token += input.mid(i, close - i + 1);
                i = close + 1;
                continue;
            }
            if (c.isSpace())
                break;
            token += c;
            ++i;
        }
        if (!token.isEmpty())
            tokens << token;
    }
    return tokens;
}

bool isQuotedPhrase(const QString &token)
{
    return token.size() >= 2 && token.startsWith(QLatin1Char('"'))
        && token.endsWith(QLatin1Char('"'));
}

bool isOperator(const QString &token, const char *name)
{
    // Uppercase only: a lowercase "or"/"and" is an ordinary search word.
    return token == QLatin1String(name);
}

// OR-groups of AND terms from free text: "a b OR c" -> [[a, b], [c]].
// Uppercase AND/OR/NOT are operators, '-' prefixed tokens are exclusions
// (collected by parseQuery, ignored here).
QVector<QStringList> planText(const QString &userText)
{
    QVector<QStringList> groups;
    QStringList current;
    const QStringList tokens = tokenizeWithQuotes(userText);
    for (const QString &token : tokens) {
        if (token.isEmpty())
            continue;
        if (isOperator(token, "OR")) {
            if (!current.isEmpty()) {
                groups.append(current);
                current.clear();
            }
            continue;
        }
        if (isOperator(token, "AND") || isOperator(token, "NOT"))
            continue; // implicit AND; NOT belongs to the exclusion list
        if (token.size() > 1 && token.startsWith(QLatin1Char('-')))
            continue; // exclusion
        current << token;
    }
    if (!current.isEmpty())
        groups.append(current);
    return groups;
}

QString escapeQuotes(QString token)
{
    // Inside a double-quoted FTS5 term only " needs escaping, by doubling it.
    token.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return token;
}

// One FTS5 term: a quoted phrase keeps its words together, a word is stripped
// of surrounding punctuation and prefix-matched. Empty when nothing is left.
QString ftsTerm(const QString &raw)
{
    if (isQuotedPhrase(raw)) {
        QString phrase = raw.mid(1, raw.size() - 2).simplified();
        if (phrase.isEmpty())
            return {};
        if (phrase.size() > kMaxTokenChars)
            phrase = phrase.left(kMaxTokenChars);
        return QStringLiteral("\"") + escapeQuotes(phrase) + QStringLiteral("\"*");
    }

    QString token = raw;
    int start = 0;
    while (start < token.size() && !token.at(start).isLetterOrNumber())
        ++start;
    int end = token.size() - 1;
    while (end >= start && !token.at(end).isLetterOrNumber())
        --end;
    if (start > end)
        return {};
    token = token.mid(start, end - start + 1);
    if (token.isEmpty() || token.size() > kMaxTokenChars)
        token = token.left(kMaxTokenChars);
    return QStringLiteral("\"") + escapeQuotes(token) + QStringLiteral("\"*");
}

// Strips one pair of surrounding quotes (if present) and unescapes doubled ones.
QString unquoteValue(const QString &value)
{
    QString out = value;
    if (isQuotedPhrase(out))
        out = out.mid(1, out.size() - 2);
    out.replace(QStringLiteral("\"\""), QStringLiteral("\""));
    return out.trimmed();
}

std::optional<int> contentTypeForName(const QString &value)
{
    const QString v = value.toLower();
    if (v == QLatin1String("text"))
        return int(ContentType::Text);
    if (v == QLatin1String("richtext") || v == QLatin1String("rich")
        || v == QLatin1String("html"))
        return int(ContentType::RichText);
    if (v == QLatin1String("image") || v == QLatin1String("images")
        || v == QLatin1String("img"))
        return int(ContentType::Image);
    if (v == QLatin1String("files") || v == QLatin1String("file")
        || v == QLatin1String("paths"))
        return int(ContentType::Files);
    return std::nullopt;
}

std::optional<bool> boolForName(const QString &value)
{
    const QString v = value.toLower();
    if (v == QLatin1String("yes") || v == QLatin1String("true") || v == QLatin1String("1")
        || v == QLatin1String("on"))
        return true;
    if (v == QLatin1String("no") || v == QLatin1String("false") || v == QLatin1String("0")
        || v == QLatin1String("off"))
        return false;
    return std::nullopt;
}

qint64 boundForDate(const QDate &date, bool endOfDay)
{
    const QDateTime moment(date, endOfDay ? QTime(23, 59, 59, 999) : QTime(0, 0));
    return moment.toMSecsSinceEpoch();
}

// "2024-01-31", "today", "yesterday", "30m", "12h", "3d", "2w".
std::optional<qint64> parseDateBound(const QString &value, bool endOfDay)
{
    const QString v = value.trimmed().toLower();
    const QDate today = QDate::currentDate();
    if (v == QLatin1String("today"))
        return boundForDate(today, endOfDay);
    if (v == QLatin1String("yesterday"))
        return boundForDate(today.addDays(-1), endOfDay);
    const QDate date = QDate::fromString(v, Qt::ISODate);
    if (date.isValid())
        return boundForDate(date, endOfDay);

    static const QRegularExpression relative(QStringLiteral(R"(^(\d+)\s*([mhdw])$)"));
    const auto match = relative.match(v);
    if (match.hasMatch()) {
        const qint64 amount = match.captured(1).toLongLong();
        const QString unit = match.captured(2);
        const qint64 unitMs = unit == QLatin1String("m") ? 60 * 1000
                            : unit == QLatin1String("h") ? 60 * 60 * 1000
                            : unit == QLatin1String("d") ? 24 * 60 * 60 * 1000
                                                         : 7 * 24 * 60 * 60 * 1000;
        return QDateTime::currentMSecsSinceEpoch() - amount * unitMs;
    }
    return std::nullopt;
}

} // namespace

bool SearchEngine::isFtsAvailable(const QSqlDatabase &db)
{
    if (!db.isOpen())
        return false;
    if (!tableExists(db, QStringLiteral("entries_fts")))
        return false;
    // Quick probe: FTS5 tables reject plain SELECT on missing tokenizer at runtime.
    QSqlQuery probe(db);
    probe.prepare(QStringLiteral("SELECT rowid FROM entries_fts LIMIT 1"));
    return probe.exec();
}

QString SearchEngine::likeEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() * 2);
    for (const QChar c : text) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

QString SearchEngine::buildFtsQuery(const QString &userText, FilterSpec::SearchScope scope)
{
    const QVector<QStringList> groups = planText(userText);
    QStringList groupExpressions;
    for (const QStringList &group : groups) {
        QStringList terms;
        for (const QString &raw : group) {
            const QString term = ftsTerm(raw);
            if (!term.isEmpty())
                terms << term;
        }
        if (!terms.isEmpty())
            groupExpressions << terms.join(QStringLiteral(" AND "));
    }
    if (groupExpressions.isEmpty())
        return {};
    // FTS5 binds AND tighter than OR, so the groups are alternatives already.
    const QString expression = groupExpressions.join(QStringLiteral(" OR "));

    // FTS5 column filter: preview : (expr) / text_data : … / ocr_text : …
    QString column;
    switch (scope) {
    case FilterSpec::SearchScope::Preview:
        column = QStringLiteral("preview");
        break;
    case FilterSpec::SearchScope::FullText:
        column = QStringLiteral("text_data");
        break;
    case FilterSpec::SearchScope::Ocr:
        column = QStringLiteral("ocr_text");
        break;
    case FilterSpec::SearchScope::All:
        break;
    }
    if (column.isEmpty())
        return expression;
    return QStringLiteral("%1 : (%2)").arg(column, expression);
}

QString SearchEngine::buildFtsTerm(const QString &term)
{
    return ftsTerm(term);
}

QVector<QStringList> SearchEngine::orGroups(const QString &userText)
{
    QVector<QStringList> groups;
    const QVector<QStringList> planned = planText(userText);
    groups.reserve(planned.size());
    for (const QStringList &group : planned) {
        QStringList terms;
        terms.reserve(group.size());
        for (const QString &raw : group) {
            const QString plain = isQuotedPhrase(raw) ? unquoteValue(raw) : raw;
            if (!plain.isEmpty())
                terms << plain;
        }
        if (!terms.isEmpty())
            groups << terms;
    }
    return groups;
}

QStringList SearchEngine::textTerms(const QString &userText)
{
    QStringList terms;
    for (const QStringList &group : orGroups(userText)) {
        for (const QString &term : group) {
            if (!terms.contains(term, Qt::CaseInsensitive))
                terms << term;
        }
    }
    return terms;
}

QString SearchEngine::negatedTerms(const QString &excludeText)
{
    QStringList terms;
    const QStringList tokens = tokenizeWithQuotes(excludeText);
    terms.reserve(tokens.size());
    for (const QString &token : tokens) {
        if (!token.isEmpty())
            terms << QStringLiteral("-") + token;
    }
    return terms.join(QLatin1Char(' '));
}

SearchEngine::ParsedQuery SearchEngine::parseQuery(const QString &input, const FilterSpec &base)
{
    ParsedQuery parsed;
    parsed.filter = base;

    QStringList free;
    QStringList excluded;
    bool negateNext = false;

    const QStringList tokens = tokenizeWithQuotes(input);
    for (const QString &token : tokens) {
        if (token.isEmpty())
            continue;

        // Uppercase NOT makes the next term an exclusion (same as -term).
        if (isOperator(token, "NOT")) {
            negateNext = true;
            continue;
        }
        if (negateNext) {
            negateNext = false;
            excluded << token;
            continue;
        }

        // -term / -"phrase": exclusion from the free-text match.
        if (token.size() > 1 && token.startsWith(QLatin1Char('-'))) {
            const QString term = token.mid(1);
            const QString plain = isQuotedPhrase(term) ? unquoteValue(term) : term;
            if (!plain.isEmpty())
                excluded << term;
            continue;
        }

        // /pattern/: guarded regular expression, verified here so the caller
        // never runs an invalid pattern.
        if (token.size() >= 2 && token.startsWith(QLatin1Char('/'))
            && token.endsWith(QLatin1Char('/'))) {
            const QString pattern = token.mid(1, token.size() - 2);
            const QRegularExpression re(pattern);
            if (pattern.isEmpty()) {
                parsed.problems << QStringLiteral("Empty /regex/ pattern");
            } else if (!re.isValid()) {
                parsed.problems << QStringLiteral("Invalid regex: %1").arg(re.errorString());
            } else if (!parsed.filter.regexText.isEmpty()) {
                parsed.problems << QStringLiteral("Only one /regex/ per search");
            } else {
                parsed.filter.regexText = pattern;
                parsed.applied << QStringLiteral("/%1/").arg(pattern);
            }
            continue;
        }

        // field:value — only when the colon comes before any quote (so URLs and
        // quoted phrases stay free text).
        const int quoteAt = token.indexOf(QLatin1Char('"'));
        const int colon = token.indexOf(QLatin1Char(':'));
        if (colon <= 0 || (quoteAt >= 0 && quoteAt < colon)) {
            free << token;
            continue;
        }

        const QString field = token.left(colon).toLower();
        const QString value = unquoteValue(token.mid(colon + 1));
        if (value.isEmpty()) {
            free << token;
            continue;
        }

        if (field == QLatin1String("app")) {
            parsed.filter.sourceApp = value;
            parsed.applied << QStringLiteral("app:%1").arg(value);
        } else if (field == QLatin1String("type")) {
            const auto type = contentTypeForName(value);
            if (type.has_value()) {
                parsed.filter.contentType = *type;
                parsed.applied << QStringLiteral("type:%1").arg(value.toLower());
            } else {
                parsed.problems << QStringLiteral("Unknown type \"%1\" (text, richtext, image, files)")
                                       .arg(value);
            }
        } else if (field == QLatin1String("tag")) {
            parsed.filter.tags << value;
            parsed.applied << QStringLiteral("tag:%1").arg(value);
        } else if (field == QLatin1String("pinned") || field == QLatin1String("sensitive")) {
            const auto flag = boolForName(value);
            if (flag.has_value()) {
                if (field == QLatin1String("pinned"))
                    parsed.filter.pinnedOnly = *flag;
                else
                    parsed.filter.sensitiveOnly = *flag;
                parsed.applied << QStringLiteral("%1:%2").arg(field, *flag ? QStringLiteral("yes")
                                                                           : QStringLiteral("no"));
            } else {
                parsed.problems << QStringLiteral("%1 expects yes or no").arg(field);
            }
        } else if (field == QLatin1String("has")) {
            if (value.compare(QLatin1String("ocr"), Qt::CaseInsensitive) == 0) {
                parsed.filter.hasOcrOnly = true;
                parsed.applied << QStringLiteral("has:ocr");
            } else {
                parsed.problems << QStringLiteral("Unknown has:\"%1\" (only has:ocr)").arg(value);
            }
        } else if (field == QLatin1String("before") || field == QLatin1String("after")) {
            const bool upperBound = field == QLatin1String("before");
            const auto bound = parseDateBound(value, upperBound);
            if (bound.has_value()) {
                if (upperBound)
                    parsed.filter.toMs = *bound;
                else
                    parsed.filter.fromMs = *bound;
                parsed.applied << QStringLiteral("%1:%2").arg(field, value.toLower());
            } else {
                parsed.problems << QStringLiteral("%1:%2 is not a date (2024-01-31, today, 7d)")
                                       .arg(field, value);
            }
        } else {
            free << token; // unknown field: ordinary text, e.g. https://…
        }
    }

    parsed.text = free.join(QLatin1Char(' '));
    parsed.filter.searchText = parsed.text;
    parsed.filter.excludeText = excluded.join(QLatin1Char(' '));
    return parsed;
}
