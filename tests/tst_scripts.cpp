#include <QtTest>

#include "ScriptActionManager.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class TestScripts : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void exampleSourceIsValid();
    void loadAndExecuteScript();
    void esModuleExportPreprocessing();
    void regexLiteralMatchParsing();
    void fallbackLabelGeneration();
    void ignoresNonTransformFiles();
    void handlesRuntimeErrors();
    void handlesSyntaxErrors();
    void rejectsOversizedInput();
    void handlesMissingScript();

private:
    QTemporaryDir m_tempDir;
};

void TestScripts::initTestCase()
{
    // Point XDG_DATA_HOME to our temporary dir so actionsDir() is isolated
    qputenv("XDG_DATA_HOME", m_tempDir.path().toUtf8());
}

void TestScripts::exampleSourceIsValid()
{
    const QString example = ScriptActionManager::exampleSource();
    QVERIFY(!example.isEmpty());
    QVERIFY(example.contains(QStringLiteral("function transform(text)")));
    QVERIFY(example.contains(QStringLiteral("var meta = {")));
}

void TestScripts::loadAndExecuteScript()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("shout.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(
        "var meta = { label: \"Shout\", match: \"^[a-z]+\" };\n"
        "function transform(text) {\n"
        "    return text.toUpperCase() + \"!\";\n"
        "}\n");
    file.close();

    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("shout")));

    const auto actions = manager.actions();
    auto it = std::find_if(actions.begin(), actions.end(), [](const ScriptAction &a) {
        return a.id == QStringLiteral("shout");
    });
    QVERIFY(it != actions.end());
    QCOMPARE(it->label, QStringLiteral("Shout"));
    QCOMPARE(it->matchPattern, QStringLiteral("^[a-z]+"));
    QVERIFY(it->hasTransform);

    auto res = manager.apply(QStringLiteral("shout"), QStringLiteral("hello"));
    QVERIFY(res.ok);
    QCOMPARE(res.output, QStringLiteral("HELLO!"));
    QVERIFY(res.error.isEmpty());
}

void TestScripts::esModuleExportPreprocessing()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("esmodule.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(
        "export function transform(text) {\n"
        "    return \"processed: \" + text;\n"
        "}\n");
    file.close();

    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("esmodule")));

    auto res = manager.apply(QStringLiteral("esmodule"), QStringLiteral("sample"));
    QVERIFY(res.ok);
    QCOMPARE(res.output, QStringLiteral("processed: sample"));
}

void TestScripts::regexLiteralMatchParsing()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("regexlit.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(
        "var meta = { label: \"Regex Lit\", match: /^\\s*\\{/ };\n"
        "function transform(text) { return text; }\n");
    file.close();

    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("regexlit")));

    const auto actions = manager.actions();
    auto it = std::find_if(actions.begin(), actions.end(), [](const ScriptAction &a) {
        return a.id == QStringLiteral("regexlit");
    });
    QVERIFY(it != actions.end());
    QCOMPARE(it->matchPattern, QStringLiteral("^\\s*\\{"));
}

void TestScripts::fallbackLabelGeneration()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("custom-snake_case-action.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("function transform(text) { return text; }\n");
    file.close();

    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("custom-snake_case-action")));

    const auto actions = manager.actions();
    auto it = std::find_if(actions.begin(), actions.end(), [](const ScriptAction &a) {
        return a.id == QStringLiteral("custom-snake_case-action");
    });
    QVERIFY(it != actions.end());
    QCOMPARE(it->label, QStringLiteral("Custom Snake Case Action"));
}

void TestScripts::ignoresNonTransformFiles()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("helper.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("// helper file without a transform function\nvar x = 42;\n");
    file.close();

    ScriptActionManager manager;
    QVERIFY(!manager.hasAction(QStringLiteral("helper")));
}

void TestScripts::handlesRuntimeErrors()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("throws.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("function transform(text) { throw new Error(\"intentional failure\"); }\n");
    file.close();

    ScriptActionManager manager;
    auto res = manager.apply(QStringLiteral("throws"), QStringLiteral("data"));
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("intentional failure")));
}

void TestScripts::handlesSyntaxErrors()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("syntax_error.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("function transform(text) { if (true { return text; } }\n");
    file.close();

    ScriptActionManager manager;
    auto res = manager.apply(QStringLiteral("syntax_error"), QStringLiteral("data"));
    QVERIFY(!res.ok);
    QVERIFY(!res.error.isEmpty());
}

void TestScripts::rejectsOversizedInput()
{
    ScriptActionManager manager;
    const QString hugeInput = QStringLiteral("a").repeated(300 * 1024); // > 256 kB
    auto res = manager.apply(QStringLiteral("shout"), hugeInput);
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("too large")));
}

void TestScripts::handlesMissingScript()
{
    ScriptActionManager manager;
    auto res = manager.apply(QStringLiteral("does_not_exist"), QStringLiteral("data"));
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("not found")));
}

QTEST_GUILESS_MAIN(TestScripts)
#include "tst_scripts.moc"
