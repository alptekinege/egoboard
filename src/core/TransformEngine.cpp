#include "TransformEngine.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {

QString htmlUnescape(const QString &in)
{
    QString out = in;
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    // numeric entities &#x...; and &#...;
    static const QRegularExpression hexRe(QStringLiteral("&#x([0-9a-fA-F]+);"));
    static const QRegularExpression decRe(QStringLiteral("&#([0-9]+);"));
    // iterative: replace hex then dec
    auto it = hexRe.globalMatch(out);
    // Avoid complexity: do simple loop with replace
    // Use while loops
    QRegularExpressionMatch m;
    while ((m = hexRe.match(out)).hasMatch()) {
        bool ok = false;
        const int code = m.captured(1).toInt(&ok, 16);
        if (ok)
            out.replace(m.captured(0), QChar(code));
        else
            break;
    }
    while ((m = decRe.match(out)).hasMatch()) {
        bool ok = false;
        const int code = m.captured(1).toInt(&ok, 10);
        if (ok)
            out.replace(m.captured(0), QChar(code));
        else
            break;
    }
    return out;
}

QString capitalizeWords(const QString &in)
{
    if (in.isEmpty())
        return in;
    QString out = in.toLower();
    bool capNext = true;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        if (capNext && c.isLetter()) {
            out[i] = c.toUpper();
            capNext = false;
        } else if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            capNext = true;
        }
    }
    return out;
}

bool isValidBase64(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty())
        return true; // empty is valid (encodes empty)
    // Strip whitespace (allow line breaks)
    QString compact;
    compact.reserve(t.size());
    for (QChar c : t) {
        if (!c.isSpace())
            compact.append(c);
    }
    if (compact.size() % 4 != 0)
        return false;
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9+/]*={0,2}$"));
    return re.match(compact).hasMatch();
}

} // namespace

QVector<TransformEngine::Descriptor> TransformEngine::allDescriptors()
{
    return {
        {TransformId::Trim, QStringLiteral("trim"), QStringLiteral("Trim"), QObject::tr("Remove leading and trailing whitespace")},
        {TransformId::Uppercase, QStringLiteral("uppercase"), QStringLiteral("Uppercase"), QObject::tr("Convert to UPPERCASE")},
        {TransformId::Lowercase, QStringLiteral("lowercase"), QStringLiteral("Lowercase"), QObject::tr("Convert to lowercase")},
        {TransformId::Capitalize, QStringLiteral("capitalize"), QStringLiteral("Capitalize"), QObject::tr("Capitalize Each Word")},
        {TransformId::Reverse, QStringLiteral("reverse"), QStringLiteral("Reverse"), QObject::tr("Reverse characters")},
        {TransformId::Base64Encode, QStringLiteral("base64-encode"), QStringLiteral("Base64 Encode"), QObject::tr("Encode as Base64 (UTF-8)")},
        {TransformId::Base64Decode, QStringLiteral("base64-decode"), QStringLiteral("Base64 Decode"), QObject::tr("Decode Base64 to UTF-8")},
        {TransformId::UrlEncode, QStringLiteral("url-encode"), QStringLiteral("URL Encode"), QObject::tr("Percent-encode for URLs")},
        {TransformId::UrlDecode, QStringLiteral("url-decode"), QStringLiteral("URL Decode"), QObject::tr("Decode percent-encoded URL")},
        {TransformId::JsonPretty, QStringLiteral("json-pretty"), QStringLiteral("JSON Pretty"), QObject::tr("Pretty-print JSON (2-space indent)")},
        {TransformId::JsonMinify, QStringLiteral("json-minify"), QStringLiteral("JSON Minify"), QObject::tr("Minify JSON (compact)")},
        {TransformId::HtmlEscape, QStringLiteral("html-escape"), QStringLiteral("HTML Escape"), QObject::tr("Escape &, <, >, \", '")},
        {TransformId::HtmlUnescape, QStringLiteral("html-unescape"), QStringLiteral("HTML Unescape"), QObject::tr("Unescape HTML entities")},
        {TransformId::SortLines, QStringLiteral("sort-lines"), QStringLiteral("Sort Lines"), QObject::tr("Sort lines alphabetically")},
        {TransformId::UniqueLines, QStringLiteral("unique-lines"), QStringLiteral("Unique Lines"), QObject::tr("Deduplicate lines, keep first occurrence")},
        {TransformId::RemoveEmptyLines, QStringLiteral("remove-empty-lines"), QStringLiteral("Remove Empty Lines"), QObject::tr("Drop blank/whitespace-only lines")},
        {TransformId::TrimLines, QStringLiteral("trim-lines"), QStringLiteral("Trim Lines"), QObject::tr("Trim whitespace from each line")},
    };
}

QVector<TransformEngine::TransformId> TransformEngine::allIds()
{
    QVector<TransformId> ids;
    const auto descs = allDescriptors();
    ids.reserve(descs.size());
    for (const auto &d : descs) ids.append(d.id);
    return ids;
}

TransformEngine::Descriptor TransformEngine::descriptor(TransformId id)
{
    for (const auto &d : allDescriptors()) {
        if (d.id == id) return d;
    }
    return {id, QStringLiteral("unknown"), QStringLiteral("Unknown"), {}};
}

QString TransformEngine::nameForId(TransformId id) { return descriptor(id).name; }
QString TransformEngine::labelForId(TransformId id) { return descriptor(id).label; }
QString TransformEngine::descriptionForId(TransformId id) { return descriptor(id).description; }

std::optional<TransformEngine::TransformId> TransformEngine::idForName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    for (const auto &d : allDescriptors()) {
        if (d.name == n) return d.id;
        // also allow label match
        if (d.label.toLower() == n) return d.id;
    }
    return std::nullopt;
}

TransformEngine::Result TransformEngine::apply(TransformId id, const QString &input)
{
    switch (id) {
    case TransformId::Trim: {
        return {true, input.trimmed(), {}};
    }
    case TransformId::Uppercase:
        return {true, input.toUpper(), {}};
    case TransformId::Lowercase:
        return {true, input.toLower(), {}};
    case TransformId::Capitalize:
        return {true, capitalizeWords(input), {}};
    case TransformId::Reverse: {
        QString out;
        out.reserve(input.size());
        for (int i = input.size() - 1; i >= 0; --i) out.append(input.at(i));
        return {true, out, {}};
    }
    case TransformId::Base64Encode: {
        return {true, QString::fromLatin1(input.toUtf8().toBase64()), {}};
    }
    case TransformId::Base64Decode: {
        if (!isValidBase64(input)) {
            return {false, {}, QObject::tr("Invalid Base64 input")};
        }
        // Remove whitespace for decode
        QString compact;
        compact.reserve(input.size());
        for (QChar c : input) if (!c.isSpace()) compact.append(c);
        const QByteArray decoded = QByteArray::fromBase64(compact.toLatin1(), QByteArray::Base64Encoding);
        // fromBase64 returns empty on invalid even if we validated; check round-trip for non-empty input
        if (compact.isEmpty())
            return {true, {}, {}};
        // Validate by re-encoding (ignoring padding variations)
        // If decoded is empty but input was not empty and not padding-only, it's invalid.
        // Our isValidBase64 already guards, so accept empty decoded only if input was empty.
        return {true, QString::fromUtf8(decoded), {}};
    }
    case TransformId::UrlEncode: {
        return {true, QString::fromLatin1(QUrl::toPercentEncoding(input)), {}};
    }
    case TransformId::UrlDecode: {
        return {true, QUrl::fromPercentEncoding(input.toUtf8()), {}};
    }
    case TransformId::JsonPretty: {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(input.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || doc.isNull()) {
            return {false, {}, QObject::tr("Invalid JSON: %1 at offset %2").arg(err.errorString()).arg(err.offset)};
        }
        return {true, QString::fromUtf8(doc.toJson(QJsonDocument::Indented)), {}};
    }
    case TransformId::JsonMinify: {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(input.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || doc.isNull()) {
            return {false, {}, QObject::tr("Invalid JSON: %1 at offset %2").arg(err.errorString()).arg(err.offset)};
        }
        return {true, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)), {}};
    }
    case TransformId::HtmlEscape:
        return {true, input.toHtmlEscaped(), {}};
    case TransformId::HtmlUnescape:
        return {true, htmlUnescape(input), {}};
    case TransformId::SortLines: {
        QStringList lines = input.split(QLatin1Char('\n'));
        std::sort(lines.begin(), lines.end());
        return {true, lines.join(QLatin1Char('\n')), {}};
    }
    case TransformId::UniqueLines: {
        QStringList lines = input.split(QLatin1Char('\n'));
        QSet<QString> seen;
        QStringList out;
        out.reserve(lines.size());
        for (const QString &l : lines) {
            if (!seen.contains(l)) {
                seen.insert(l);
                out.append(l);
            }
        }
        return {true, out.join(QLatin1Char('\n')), {}};
    }
    case TransformId::RemoveEmptyLines: {
        QStringList lines = input.split(QLatin1Char('\n'));
        QStringList out;
        out.reserve(lines.size());
        for (const QString &l : lines) {
            if (!l.trimmed().isEmpty()) out.append(l);
        }
        return {true, out.join(QLatin1Char('\n')), {}};
    }
    case TransformId::TrimLines: {
        QStringList lines = input.split(QLatin1Char('\n'));
        for (QString &l : lines) l = l.trimmed();
        return {true, lines.join(QLatin1Char('\n')), {}};
    }
    }
    return {false, {}, QObject::tr("Unknown transform")};
}

TransformEngine::Result TransformEngine::applyChain(const QString &input, const QList<TransformId> &chain)
{
    QString cur = input;
    for (TransformId id : chain) {
        Result r = apply(id, cur);
        if (!r.ok) {
            r.error = QStringLiteral("%1 → %2").arg(nameForId(id), r.error);
            return r;
        }
        cur = r.output;
    }
    return {true, cur, {}};
}

TransformEngine::Result TransformEngine::applyChainByNames(const QString &input, const QStringList &names, QString *failedName)
{
    QList<TransformId> chain;
    chain.reserve(names.size());
    for (const QString &n : names) {
        auto id = idForName(n);
        if (!id.has_value()) {
            if (failedName) *failedName = n;
            return {false, {}, QObject::tr("Unknown transform: %1").arg(n)};
        }
        chain.append(*id);
    }
    return applyChain(input, chain);
}
