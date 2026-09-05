#include <QtTest>

#include "CodePreviewHighlighter.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QTextDocument>

class TestUiLogic : public QObject
{
    Q_OBJECT

private slots:
    void detectsPreviewModes();
    void plainModeLeavesTextUnformatted();
    void jsonModeFormatsKeysStringsNumbersAndBooleans();
    void xmlModeFormatsTagsAndAttributes();
    void codeModeFormatsKeywordsCommentsAndStrings();
    void themePaletteSwitchesAndRestores();
};

void TestUiLogic::detectsPreviewModes()
{
    QCOMPARE(CodePreviewHighlighter::detect(QString()), CodePreviewHighlighter::Mode::Plain);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("ordinary clipboard text")),
             CodePreviewHighlighter::Mode::Plain);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral(R"({"name":"egoboard","count":2})")),
             CodePreviewHighlighter::Mode::Json);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("<root><item>value</item></root>")),
             CodePreviewHighlighter::Mode::Xml);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("function run() { return 'ok'; }")),
             CodePreviewHighlighter::Mode::Code);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("def run():\n    return 1")),
             CodePreviewHighlighter::Mode::Code);
    QCOMPARE(CodePreviewHighlighter::detect(QStringLiteral("{ not actually structured }")),
             CodePreviewHighlighter::Mode::Plain);
}

void TestUiLogic::plainModeLeavesTextUnformatted()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("plain text 42"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Plain);
    const QTextBlock block = document.begin();
    QVERIFY(block.isValid());
    QCOMPARE(block.layout()->formats().size(), 0);
}

void TestUiLogic::jsonModeFormatsKeysStringsNumbersAndBooleans()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral(R"({"name":"egoboard","count":42,"enabled":true})"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Json);

    const QList<QTextLayout::FormatRange> formats = document.begin().layout()->formats();
    QVERIFY(formats.size() >= 4);
    const QString text = document.toPlainText();
    const auto hasFormatAt = [&formats](int position) {
        for (const auto &range : formats) {
            if (position >= range.start && position < range.start + range.length)
                return true;
        }
        return false;
    };
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("name"))));
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("egoboard"))));
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("42"))));
    QVERIFY(hasFormatAt(text.indexOf(QStringLiteral("true"))));
}

void TestUiLogic::xmlModeFormatsTagsAndAttributes()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("<item id=\"7\">value</item>"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Xml);

    const auto formats = document.begin().layout()->formats();
    QVERIFY(formats.size() >= 2);
    const QString text = document.toPlainText();
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QLatin1Char('<'));
    }));
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("id"));
    }));
}

void TestUiLogic::codeModeFormatsKeywordsCommentsAndStrings()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("return \"value\"; // comment"));
    CodePreviewHighlighter highlighter(&document);
    highlighter.setMode(CodePreviewHighlighter::Mode::Code);

    const auto formats = document.begin().layout()->formats();
    QVERIFY(formats.size() >= 3);
    const QString text = document.toPlainText();
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("return"));
    }));
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("\"value\""));
    }));
    QVERIFY(std::any_of(formats.cbegin(), formats.cend(), [&text](const QTextLayout::FormatRange &range) {
        return range.start == text.indexOf(QStringLiteral("// comment"));
    }));
}

void TestUiLogic::themePaletteSwitchesAndRestores()
{
    const QPalette original = qApp->palette();

    ThemeManager::apply(QStringLiteral("light"), nullptr);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(0xef, 0xf0, 0xf2));
    QCOMPARE(qApp->palette().color(QPalette::Base), QColor(0xfc, 0xfc, 0xfc));

    ThemeManager::apply(QStringLiteral("dark"), nullptr);
    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(0x2a, 0x2e, 0x32));
    QCOMPARE(qApp->palette().color(QPalette::Base), QColor(0x1b, 0x1e, 0x21));

    ThemeManager::apply(QStringLiteral("invalid"), nullptr);
    QCOMPARE(qApp->palette(), QApplication::style()->standardPalette());
    qApp->setPalette(original);
}

QTEST_MAIN(TestUiLogic)
#include "tst_ui.moc"
