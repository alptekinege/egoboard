#include "SnippetManager.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

SnippetManager::SnippetManager(QSqlDatabase db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

qint64 SnippetManager::createSnippet(const QString &name, const QString &templateText, const QString &shortcut)
{
    if (name.trimmed().isEmpty() || templateText.isEmpty())
        return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO snippets (name, template, shortcut, created_ms) VALUES (:n, :t, :s, :c)"));
    q.bindValue(QStringLiteral(":n"), name.trimmed());
    q.bindValue(QStringLiteral(":t"), templateText);
    q.bindValue(QStringLiteral(":s"), shortcut.trimmed());
    q.bindValue(QStringLiteral(":c"), QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        qWarning("egoboard: createSnippet failed: %s", qPrintable(q.lastError().text()));
        return 0;
    }
    emit snippetsChanged();
    return q.lastInsertId().toLongLong();
}

bool SnippetManager::updateSnippet(qint64 id, const QString &name, const QString &templateText, const QString &shortcut)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE snippets SET name = :n, template = :t, shortcut = :s WHERE id = :id"));
    q.bindValue(QStringLiteral(":n"), name.trimmed());
    q.bindValue(QStringLiteral(":t"), templateText);
    q.bindValue(QStringLiteral(":s"), shortcut.trimmed());
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec() || q.numRowsAffected() == 0)
        return false;
    emit snippetsChanged();
    return true;
}

bool SnippetManager::deleteSnippet(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM snippets WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec() || q.numRowsAffected() == 0)
        return false;
    emit snippetsChanged();
    return true;
}

QVector<Snippet> SnippetManager::snippets() const
{
    QVector<Snippet> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, name, template, shortcut, created_ms FROM snippets ORDER BY name COLLATE NOCASE"))) {
        qWarning("egoboard: snippets() failed: %s", qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        Snippet s;
        s.id = q.value(0).toLongLong();
        s.name = q.value(1).toString();
        s.templateText = q.value(2).toString();
        s.shortcut = q.value(3).toString();
        s.createdMs = q.value(4).toLongLong();
        out.append(s);
    }
    return out;
}

std::optional<Snippet> SnippetManager::snippet(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name, template, shortcut, created_ms FROM snippets WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    Snippet s;
    s.id = q.value(0).toLongLong();
    s.name = q.value(1).toString();
    s.templateText = q.value(2).toString();
    s.shortcut = q.value(3).toString();
    s.createdMs = q.value(4).toLongLong();
    return s;
}

QString SnippetManager::expand(const QString &tmpl, const QString &clipboardText)
{
    if (tmpl.isEmpty())
        return tmpl;
    const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    const QString datetime = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());

    // Support {{clipboard}}, {{text}}, {{selection}} as aliases; also {{date}}, {{time}}, {{datetime}}, {{timestamp}}, {{ts}}
    // We also support whitespace inside braces: {{ clipboard }}.
    // Use regex to be safe for future extensions; fallback to simple replace for speed on trivial templates.
    QString out = tmpl;
    // Fast path: direct replacements for the common forms (no regex overhead for most snippet)
    out.replace(QStringLiteral("{{clipboard}}"), clipboardText);
    out.replace(QStringLiteral("{{text}}"), clipboardText);
    out.replace(QStringLiteral("{{selection}}"), clipboardText);
    out.replace(QStringLiteral("{{date}}"), date);
    out.replace(QStringLiteral("{{time}}"), time);
    out.replace(QStringLiteral("{{datetime}}"), datetime);
    out.replace(QStringLiteral("{{timestamp}}"), ts);
    out.replace(QStringLiteral("{{ts}}"), ts);

    // Regex for whitespace variants and case where clipboard alias appears with spaces
    // e.g. {{ clipboard }}, {{ DATE }} — we normalize to lower for those.
    static const QRegularExpression re(QStringLiteral("\\{\\{\\s*(\\w+)\\s*\\}\\}"));
    // Only run regex if there are still braces left
    if (out.contains(QStringLiteral("{{"))) {
        QString result;
        result.reserve(out.size() * 2);
        int lastPos = 0;
        auto it = re.globalMatch(out);
        while (it.hasNext()) {
            const auto m = it.next();
            result.append(out.mid(lastPos, m.capturedStart() - lastPos));
            const QString key = m.captured(1).toLower();
            if (key == QLatin1String("clipboard") || key == QLatin1String("text") || key == QLatin1String("selection"))
                result.append(clipboardText);
            else if (key == QLatin1String("date"))
                result.append(date);
            else if (key == QLatin1String("time"))
                result.append(time);
            else if (key == QLatin1String("datetime"))
                result.append(datetime);
            else if (key == QLatin1String("timestamp") || key == QLatin1String("ts"))
                result.append(ts);
            else
                result.append(m.captured(0)); // unknown placeholder — keep as-is
            lastPos = m.capturedEnd();
        }
        result.append(out.mid(lastPos));
        out = result;
    }
    return out;
}

QString SnippetManager::expandSnippet(qint64 id, const QString &clipboardText) const
{
    auto s = snippet(id);
    if (!s.has_value()) return {};
    return expand(s->templateText, clipboardText);
}
