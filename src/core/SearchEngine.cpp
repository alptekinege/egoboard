#include "SearchEngine.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace {

bool tableExists(const QSqlDatabase &db, const QString &name)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=:n LIMIT 1"));
    q.bindValue(QStringLiteral(":n"), name);
    if (!q.exec() || !q.next())
        return false;
    return true;
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

QString SearchEngine::escapeFtsToken(QString token)
{
    // FTS5 special chars to neutralize when inside quoted term.
    // Inside double quotes only " needs escaping by doubling it.
    token.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return token;
}

QString SearchEngine::buildFtsQuery(const QString &userText)
{
    const QString trimmed = userText.trimmed();
    if (trimmed.isEmpty())
        return {};

    // Split on whitespace, drop empty parts. Preserve order for ranking.
    const QStringList rawTokens = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (rawTokens.isEmpty())
        return {};

    QStringList ftsTokens;
    ftsTokens.reserve(rawTokens.size());
    for (QString token : rawTokens) {
        // Keep only meaningful tokens (>=1 alnum). Strip surrounding punctuation.
        int start = 0;
        while (start < token.size() && !token.at(start).isLetterOrNumber())
            ++start;
        int end = token.size() - 1;
        while (end >= start && !token.at(end).isLetterOrNumber())
            --end;
        if (start > end)
            continue;
        token = token.mid(start, end - start + 1);
        if (token.isEmpty() || token.size() > 64)
            token = token.left(64);
        token = escapeFtsToken(token);
        // Prefix search: "token"*  — quoted for safety + wildcard for responsiveness.
        ftsTokens << QStringLiteral("\"") + token + QStringLiteral("\"*");
    }
    if (ftsTokens.isEmpty())
        return {};
    // AND semantics: every word must appear (feels like classic clipboard search).
    return ftsTokens.join(QStringLiteral(" AND "));
}
