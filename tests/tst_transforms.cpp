#include <QtTest>
#include "TransformEngine.h"

class TestTransforms : public QObject {
    Q_OBJECT
private slots:
    void trim();
    void caseTransforms();
    void reverse();
    void base64();
    void base64Invalid();
    void url();
    void jsonPretty();
    void jsonMinify();
    void jsonInvalid();
    void htmlEscape();
    void htmlUnescape();
    void sortLines();
    void uniqueLines();
    void removeEmptyLines();
    void trimLines();
    void chain();
    void chainByNames();
    void chainFails();
    void allDescriptors();
};

void TestTransforms::trim()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::Trim, QStringLiteral("  hello  "));
    QVERIFY(r.ok);
    QCOMPARE(r.output, QStringLiteral("hello"));
}

void TestTransforms::caseTransforms()
{
    QCOMPARE(TransformEngine::apply(TransformEngine::TransformId::Uppercase, QStringLiteral("hello")).output, QStringLiteral("HELLO"));
    QCOMPARE(TransformEngine::apply(TransformEngine::TransformId::Lowercase, QStringLiteral("HELLO")).output, QStringLiteral("hello"));
    auto cap = TransformEngine::apply(TransformEngine::TransformId::Capitalize, QStringLiteral("hello world-foo_bar"));
    QVERIFY(cap.ok);
    QCOMPARE(cap.output, QStringLiteral("Hello World-Foo_Bar"));
}

void TestTransforms::reverse()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::Reverse, QStringLiteral("abc"));
    QCOMPARE(r.output, QStringLiteral("cba"));
}

void TestTransforms::base64()
{
    auto enc = TransformEngine::apply(TransformEngine::TransformId::Base64Encode, QStringLiteral("hello"));
    QVERIFY(enc.ok);
    QCOMPARE(enc.output, QStringLiteral("aGVsbG8="));
    auto dec = TransformEngine::apply(TransformEngine::TransformId::Base64Decode, enc.output);
    QVERIFY(dec.ok);
    QCOMPARE(dec.output, QStringLiteral("hello"));
}

void TestTransforms::base64Invalid()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::Base64Decode, QStringLiteral("!!!not_base64!!!"));
    QVERIFY(!r.ok);
}

void TestTransforms::url()
{
    auto enc = TransformEngine::apply(TransformEngine::TransformId::UrlEncode, QStringLiteral("hello world &"));
    QVERIFY(enc.ok);
    QVERIFY(enc.output.contains(QStringLiteral("%20")));
    auto dec = TransformEngine::apply(TransformEngine::TransformId::UrlDecode, enc.output);
    QVERIFY(dec.ok);
    QCOMPARE(dec.output, QStringLiteral("hello world &"));
}

void TestTransforms::jsonPretty()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::JsonPretty, QStringLiteral("{\"a\":1,\"b\":[2,3]}"));
    QVERIFY(r.ok);
    QVERIFY(r.output.contains(QStringLiteral("\"a\"")));
    QVERIFY(r.output.contains(QStringLiteral("\n")));
}

void TestTransforms::jsonMinify()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::JsonMinify, QStringLiteral("{\n  \"a\": 1\n}"));
    QVERIFY(r.ok);
    QCOMPARE(r.output.trimmed(), QStringLiteral("{\"a\":1}"));
}

void TestTransforms::jsonInvalid()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::JsonPretty, QStringLiteral("{not json"));
    QVERIFY(!r.ok);
}

void TestTransforms::htmlEscape()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::HtmlEscape, QStringLiteral("<a & b>"));
    QVERIFY(r.ok);
    QVERIFY(r.output.contains(QStringLiteral("&lt;")));
    QVERIFY(r.output.contains(QStringLiteral("&amp;")));
}

void TestTransforms::htmlUnescape()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::HtmlUnescape, QStringLiteral("&lt;a &amp; b&gt;"));
    QVERIFY(r.ok);
    QCOMPARE(r.output, QStringLiteral("<a & b>"));
}

void TestTransforms::sortLines()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::SortLines, QStringLiteral("b\na\nc"));
    QCOMPARE(r.output, QStringLiteral("a\nb\nc"));
}

void TestTransforms::uniqueLines()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::UniqueLines, QStringLiteral("a\nb\na\nc\nb"));
    QCOMPARE(r.output, QStringLiteral("a\nb\nc"));
}

void TestTransforms::removeEmptyLines()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::RemoveEmptyLines, QStringLiteral("a\n\n  \nb\n\nc"));
    QCOMPARE(r.output, QStringLiteral("a\nb\nc"));
}

void TestTransforms::trimLines()
{
    auto r = TransformEngine::apply(TransformEngine::TransformId::TrimLines, QStringLiteral("  a  \n  b \n c"));
    QCOMPARE(r.output, QStringLiteral("a\nb\nc"));
}

void TestTransforms::chain()
{
    auto r = TransformEngine::applyChain(QStringLiteral("  hello world  "), {TransformEngine::TransformId::Trim, TransformEngine::TransformId::Uppercase});
    QVERIFY(r.ok);
    QCOMPARE(r.output, QStringLiteral("HELLO WORLD"));
}

void TestTransforms::chainByNames()
{
    QString failed;
    auto r = TransformEngine::applyChainByNames(QStringLiteral("  hello  "), QStringList{QStringLiteral("trim"), QStringLiteral("uppercase")}, &failed);
    QVERIFY(r.ok);
    QCOMPARE(r.output, QStringLiteral("HELLO"));
    auto bad = TransformEngine::applyChainByNames(QStringLiteral("hi"), QStringList{QStringLiteral("nonexistent")}, &failed);
    QVERIFY(!bad.ok);
    QCOMPARE(failed, QStringLiteral("nonexistent"));
}

void TestTransforms::chainFails()
{
    // Base64 decode after trim should fail if input invalid
    auto r = TransformEngine::applyChain(QStringLiteral("!!!"), {TransformEngine::TransformId::Base64Decode});
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("base64-decode")));
}

void TestTransforms::allDescriptors()
{
    auto descs = TransformEngine::allDescriptors();
    QVERIFY(descs.size() >= 17);
    for (const auto &d : descs) {
        QVERIFY(!d.name.isEmpty());
        QVERIFY(!d.label.isEmpty());
        auto id = TransformEngine::idForName(d.name);
        QVERIFY(id.has_value());
        QCOMPARE(static_cast<int>(*id), static_cast<int>(d.id));
    }
}

QTEST_GUILESS_MAIN(TestTransforms)
#include "tst_transforms.moc"
