#include "CodePreviewHighlighter.h"
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>

namespace {
// Dark-surface variants of the VS-style colors (light variants are unreadable
// on a dark Base color).
struct ThemeFormats {
    QTextCharFormat stringFmt;
    QTextCharFormat keyFmt;
    QTextCharFormat numberFmt;
    QTextCharFormat keywordFmt;
    QTextCharFormat commentFmt;
};

bool isDarkSurface()
{
    return QGuiApplication::palette().color(QPalette::Base).lightness() < 128;
}

ThemeFormats makeFormats()
{
    ThemeFormats f;
    if (!isDarkSurface()) {
        f.stringFmt.setForeground(QColor(QStringLiteral("#a31515")));
        f.keyFmt.setForeground(QColor(QStringLiteral("#0451a5")));
        f.keyFmt.setFontWeight(QFont::DemiBold);
        f.numberFmt.setForeground(QColor(QStringLiteral("#098658")));
        f.keywordFmt.setForeground(QColor(QStringLiteral("#0000ff")));
        f.commentFmt.setForeground(QColor(QStringLiteral("#008000")));
    } else {
        // Breeze-dark code colors: readable on #1b1e21
        f.stringFmt.setForeground(QColor(QStringLiteral("#f67400")));   // orange strings
        f.keyFmt.setForeground(QColor(QStringLiteral("#8e44ad")));     // violet keys
        f.keyFmt.setFontWeight(QFont::DemiBold);
        f.numberFmt.setForeground(QColor(QStringLiteral("#f67400")));  // orange numbers
        f.keywordFmt.setForeground(QColor(QStringLiteral("#1d99f3"))); // blue keywords
        f.commentFmt.setForeground(QColor(QStringLiteral("#7f8c8d"))); // gray comments
    }
    f.commentFmt.setFontItalic(true);
    return f;
}
} // namespace

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
    const ThemeFormats tf = makeFormats();
    const QTextCharFormat &stringFmt = tf.stringFmt;
    const QTextCharFormat &keyFmt = tf.keyFmt;
    const QTextCharFormat &numberFmt = tf.numberFmt;
    const QTextCharFormat &keywordFmt = tf.keywordFmt;
    const QTextCharFormat &commentFmt = tf.commentFmt;

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
