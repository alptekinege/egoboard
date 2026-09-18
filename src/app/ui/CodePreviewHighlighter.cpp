#include "CodePreviewHighlighter.h"
#include "../TextAppearance.h"
#include <QGuiApplication>
#include <QRegularExpression>

namespace {
// Hue offsets from the scheme's own accent: a blue-accented scheme (Breeze)
// lands on the familiar code look — blue keywords, violet keys, warm strings,
// green numbers — while any other accent still yields a coherent set.
constexpr int kKeyHueShift = 70;
constexpr int kStringHueShift = 180;
constexpr int kNumberHueShift = -60;
constexpr int kFallbackAccentHue = 205; // Breeze blue

int accentHue(const QPalette &palette)
{
    int hue = palette.color(QPalette::Highlight).hsvHue();
    if (hue < 0)
        hue = palette.color(QPalette::Link).hsvHue();
    return hue < 0 ? kFallbackAccentHue : hue;
}

QColor roleColor(const QPalette &palette, int hue, qreal saturation, qreal value)
{
    const QColor surface = palette.color(QPalette::Base);
    const QColor seed = QColor::fromHsv((hue % 360 + 360) % 360, qRound(saturation * 255),
                                        qRound(value * 255));
    // Every role is held to the same floor as ordinary text, so a scheme whose
    // accent happens to be close to its Base still produces readable code.
    return TextAppearance::ensureContrast(seed, surface, TextAppearance::kTextContrastRatio);
}

// Built once per palette, not once per highlighted block.
const CodePreviewHighlighter::Theme &currentTheme()
{
    static CodePreviewHighlighter::Theme theme;
    static qint64 paletteKey = 0;
    const QPalette palette = QGuiApplication::palette();
    if (paletteKey != palette.cacheKey()) {
        theme = CodePreviewHighlighter::themeFor(palette);
        paletteKey = palette.cacheKey();
    }
    return theme;
}
} // namespace

CodePreviewHighlighter::Theme CodePreviewHighlighter::themeFor(const QPalette &palette)
{
    const QColor surface = palette.color(QPalette::Base);
    const bool dark = surface.lightness() < 128;
    const int accent = accentHue(palette);
    const qreal saturation = dark ? 0.78 : 0.88;
    const qreal value = dark ? 0.94 : 0.55;

    Theme theme;
    theme.keyword = roleColor(palette, accent, saturation, value);
    theme.key = roleColor(palette, accent + kKeyHueShift, saturation * 0.75, value);
    theme.string = roleColor(palette, accent + kStringHueShift, saturation, value);
    theme.number = roleColor(palette, accent + kNumberHueShift, saturation * 0.9, value);
    // Comments use the scheme's own subdued role, italic and slightly dimmer
    // than code (the readability floor for secondary text, not for body text).
    theme.comment = TextAppearance::ensureContrast(palette.color(QPalette::Mid), surface,
                                                   TextAppearance::kDimTextContrastRatio);
    return theme;
}

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
    const Theme theme = currentTheme();

    QTextCharFormat stringFmt;
    stringFmt.setForeground(theme.string);
    QTextCharFormat keyFmt;
    keyFmt.setForeground(theme.key);
    keyFmt.setFontWeight(QFont::DemiBold);
    QTextCharFormat numberFmt;
    numberFmt.setForeground(theme.number);
    QTextCharFormat keywordFmt;
    keywordFmt.setForeground(theme.keyword);
    QTextCharFormat commentFmt;
    commentFmt.setForeground(theme.comment);
    commentFmt.setFontItalic(true);

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
