#include "CodePreviewHighlighter.h"
#include <QRegularExpression>

CodePreviewHighlighter::CodePreviewHighlighter(QTextDocument *doc)
    : QSyntaxHighlighter(doc) {}

void CodePreviewHighlighter::setMode(Mode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    rehighlight();
}

CodePreviewHighlighter::Mode CodePreviewHighlighter::detect(const QString &text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return Mode::Plain;
    if ((t.startsWith(QLatin1Char('{')) && t.endsWith(QLatin1Char('}'))) ||
        (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']')))) {
        // quick JSON check: contains quotes and colon
        if (t.contains(QLatin1Char(':')) && t.contains(QLatin1Char('"')))
            return Mode::Json;
    }
    if (t.startsWith(QStringLiteral("<?xml")) || (t.startsWith(QLatin1Char('<')) && t.contains(QLatin1Char('>')) && t.contains(QStringLiteral("</"))))
        return Mode::Xml;
    // generic code: many semicolons/braces/keywords
    int score = 0;
    if (t.contains(QStringLiteral("function")) || t.contains(QStringLiteral("class ")) || t.contains(QStringLiteral("import "))) score++;
    if (t.contains(QLatin1Char(';')) && t.contains(QLatin1Char('{'))) score++;
    if (t.contains(QStringLiteral("def ")) || t.contains(QStringLiteral("const ")) || t.contains(QStringLiteral("let "))) score++;
    return score > 0 ? Mode::Code : Mode::Plain;
}

void CodePreviewHighlighter::highlightBlock(const QString &text) {
    if (m_mode == Mode::Plain) return;
    // Common formats
    QTextCharFormat stringFmt; stringFmt.setForeground(QColor(QStringLiteral("#a31515")));
    QTextCharFormat keyFmt; keyFmt.setForeground(QColor(QStringLiteral("#0451a5"))); keyFmt.setFontWeight(QFont::DemiBold);
    QTextCharFormat numberFmt; numberFmt.setForeground(QColor(QStringLiteral("#098658")));
    QTextCharFormat keywordFmt; keywordFmt.setForeground(QColor(QStringLiteral("#0000ff")));
    QTextCharFormat commentFmt; commentFmt.setForeground(QColor(QStringLiteral("#008000"))); commentFmt.setFontItalic(true);

    if (m_mode == Mode::Json) {
        // strings "key" :
        static const QRegularExpression keyRe(QStringLiteral("\"([^\"]+)\"\\s*:")); 
        auto it = keyRe.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(1)-1, m.capturedLength(1)+2, keyFmt); }
        static const QRegularExpression strRe(QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));
        it = strRe.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); if (format(m.capturedStart()) == keyFmt) continue; setFormat(m.capturedStart(), m.capturedLength(), stringFmt); }
        static const QRegularExpression numRe(QStringLiteral("\\b-?\\d+\\.?\\d*\\b"));
        it = numRe.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), numberFmt); }
        // booleans/null
        static const QRegularExpression boolRe(QStringLiteral("\\b(true|false|null)\\b"));
        it = boolRe.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), keywordFmt); }
        return;
    }
    if (m_mode == Mode::Xml) {
        static const QRegularExpression tagRe(QStringLiteral("</?[^>]+>"));
        auto it = tagRe.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), keywordFmt); }
        static const QRegularExpression attrRe(QStringLiteral("\\b\\w+\\s*=\\s*\"[^\"]*\""));
        it = attrRe.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), stringFmt); }
        return;
    }
    // Generic code
    static const QRegularExpression kwRe(QStringLiteral("\\b(if|else|for|while|return|class|function|import|export|const|let|var|def|public|private|static|void|int|string|bool)\\b"));
    auto it = kwRe.globalMatch(text);
    while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), keywordFmt); }
    static const QRegularExpression commentRe(QStringLiteral("//[^\n]*|#.*"));
    it = commentRe.globalMatch(text);
    while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), commentFmt); }
    static const QRegularExpression str2Re(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"));
    it = str2Re.globalMatch(text);
    while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), stringFmt); }
}
