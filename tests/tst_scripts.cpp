#include <QtTest>

#include "ScriptActionManager.h"

#include <QDir>
#include <QElapsedTimer>
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
    void handlesNullAndUndefinedResults();
    void reloadRemovesDeletedActions();
    void timesOutRunawayScripts();
    void supportsArrowFunctionTransforms();
    void sandboxExposesNoHostApis();
    void sandboxBlocksExfiltrationAttempt();
    void globalScopeHoldsNoHostObjects();
    void timesOutTopLevelLoop();
    void slowScriptWithinBudgetSucceeds();
    void engineReusableAfterTimeout();
    void skipsOversizedScriptFile();
    void skipsEmptyAndNonJsFiles();
    void inputSizeBoundaries();
    void emptyInput();
    void transformNotCallableAtRuntime();
    void errorMessagesCarryCallSite();
    void exampleTemplateEvaluates();
    void exportFormPreprocessing();
    void metaQuotingAndDefaults();
    void applyUsesCachedSourceUntilReload();

private:
    void writeScript(const QString &name, const QByteArray &source);
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

void TestScripts::handlesNullAndUndefinedResults()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString nullPath = QDir(dirPath).filePath(QStringLiteral("null-result.js"));
    QFile nullFile(nullPath);
    QVERIFY(nullFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(nullFile.write("function transform(text) { return null; }\n") > 0);
    nullFile.close();

    const QString undefinedPath = QDir(dirPath).filePath(QStringLiteral("undefined-result.js"));
    QFile undefinedFile(undefinedPath);
    QVERIFY(undefinedFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(undefinedFile.write("function transform(text) { }\n") > 0);
    undefinedFile.close();

    ScriptActionManager manager;
    const auto nullResult = manager.apply(QStringLiteral("null-result"), QStringLiteral("data"));
    QVERIFY(nullResult.ok);
    QCOMPARE(nullResult.output, QString());
    QVERIFY(nullResult.error.isEmpty());

    const auto undefinedResult = manager.apply(QStringLiteral("undefined-result"), QStringLiteral("data"));
    QVERIFY(undefinedResult.ok);
    QCOMPARE(undefinedResult.output, QString());
    QVERIFY(undefinedResult.error.isEmpty());
}

void TestScripts::reloadRemovesDeletedActions()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);
    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("reloadable.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(file.write("function transform(text) { return text; }\n") > 0);
    file.close();

    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("reloadable")));
    QVERIFY(QFile::remove(scriptPath));
    manager.reload();
    QVERIFY(!manager.hasAction(QStringLiteral("reloadable")));
    const auto result = manager.apply(QStringLiteral("reloadable"), QStringLiteral("data"));
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("not found")));
}

void TestScripts::timesOutRunawayScripts()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("runaway.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(file.write("function transform(text) { while (true) {} }\n") > 0);
    file.close();

    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("runaway")));

    QElapsedTimer timer;
    timer.start();
    const auto res = manager.apply(QStringLiteral("runaway"), QStringLiteral("data"));
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("timed out")));
    QVERIFY(timer.elapsed() < 10000); // interrupted, not hung
}

void TestScripts::supportsArrowFunctionTransforms()
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);

    const QString scriptPath = QDir(dirPath).filePath(QStringLiteral("arrow.js"));
    QFile file(scriptPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(file.write(
        "var meta = { label: \"Arrow\" };\n"
        "const transform = (text) => text.split('').reverse().join('');\n") > 0);
    file.close();

    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("arrow")));
    const auto res = manager.apply(QStringLiteral("arrow"), QStringLiteral("abc"));
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.output, QStringLiteral("cba"));
}

void TestScripts::writeScript(const QString &name, const QByteArray &source)
{
    const QString dirPath = ScriptActionManager::actionsDir();
    QDir().mkpath(dirPath);
    QFile file(QDir(dirPath).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(file.write(source) >= 0);
    file.close();
}

void TestScripts::sandboxExposesNoHostApis()
{
    // The sandbox claim: a bare QJSEngine with no file/network globals and
    // no exposed Qt/C++ objects. Every host API probe must be undefined.
    writeScript(QStringLiteral("sb-typeof.js"),
                "function transform(text) {\n"
                "  return [typeof XMLHttpRequest, typeof fetch, typeof require,\n"
                "          typeof process, typeof Qt, typeof QFile, typeof console,\n"
                "          typeof module].join(\",\");\n"
                "}\n");
    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("sb-typeof")));
    const auto res = manager.apply(QStringLiteral("sb-typeof"), QStringLiteral("data"));
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.output, QStringLiteral("undefined,undefined,undefined,undefined,"
                                        "undefined,undefined,undefined,undefined"));
}

void TestScripts::sandboxBlocksExfiltrationAttempt()
{
    writeScript(QStringLiteral("sb-exfil.js"),
                "function transform(text) {\n"
                "  try {\n"
                "    if (typeof require !== \"undefined\")\n"
                "      return require(\"fs\").readFileSync(\"/etc/passwd\");\n"
                "    return \"no-require\";\n"
                "  } catch (e) { return \"blocked\"; }\n"
                "}\n");
    ScriptActionManager manager;
    const auto res = manager.apply(QStringLiteral("sb-exfil"), QStringLiteral("data"));
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.output, QStringLiteral("no-require"));
}

void TestScripts::globalScopeHoldsNoHostObjects()
{
    // Enumerate the global object via the Function constructor (globalThis
    // is not defined in this Qt's engine): standard builtins only, none of
    // the host names a script could pivot through.
    writeScript(QStringLiteral("sb-globals.js"),
                "function transform(text) {\n"
                "  var g = Function('return this')();\n"
                "  var names = Object.getOwnPropertyNames(g).join(\",\");\n"
                "  var banned = [\"XMLHttpRequest\", \"fetch\", \"require\", \"process\",\n"
                "                \"Qt\", \"console\", \"module\", \"exports\", \"gc\",\n"
                "                \"print\", \"QFile\", \"alert\"];\n"
                "  var found = [];\n"
                "  for (var i = 0; i < banned.length; i++) {\n"
                "    if ((\",\" + names + \",\").indexOf(\",\" + banned[i] + \",\") !== -1)\n"
                "      found.push(banned[i]);\n"
                "  }\n"
                "  return found.join(\"|\");\n"
                "}\n");
    ScriptActionManager manager;
    const auto res = manager.apply(QStringLiteral("sb-globals"), QStringLiteral("data"));
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.output, QString());
}

void TestScripts::timesOutTopLevelLoop()
{
    // The watchdog guards evaluate() too, not just the transform() call: an
    // infinite loop at file top level is interrupted on the same budget.
    writeScript(QStringLiteral("tmo-toplevel.js"),
                "while (true) {}\n"
                "function transform(text) { return text; }\n");
    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("tmo-toplevel")));

    QElapsedTimer timer;
    timer.start();
    const auto res = manager.apply(QStringLiteral("tmo-toplevel"), QStringLiteral("data"));
    const qint64 elapsed = timer.elapsed();
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("timed out")));
    QVERIFY2(elapsed >= 1900, qPrintable(QString::number(elapsed)));
    QVERIFY2(elapsed < 10000, qPrintable(QString::number(elapsed)));
}

void TestScripts::slowScriptWithinBudgetSucceeds()
{
    // A legitimately slow script (~0.8 s busy loop) must not be killed: the
    // watchdog fires at the 2 s deadline, not before.
    writeScript(QStringLiteral("tmo-slow.js"),
                "function transform(text) {\n"
                "  var end = Date.now() + 800;\n"
                "  while (Date.now() < end) {}\n"
                "  return text;\n"
                "}\n");
    ScriptActionManager manager;
    const auto res = manager.apply(QStringLiteral("tmo-slow"), QStringLiteral("data"));
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.output, QStringLiteral("data"));
}

void TestScripts::engineReusableAfterTimeout()
{
    writeScript(QStringLiteral("tmo-once.js"),
                "function transform(text) { while (true) {} }\n");
    writeScript(QStringLiteral("tmo-echo.js"),
                "function transform(text) { return \"echo:\" + text; }\n");
    ScriptActionManager manager;
    const auto timedOut = manager.apply(QStringLiteral("tmo-once"), QStringLiteral("data"));
    QVERIFY(!timedOut.ok);
    QVERIFY(timedOut.error.contains(QStringLiteral("timed out")));
    // The interrupt is per-engine: later applies on other actions still work.
    const auto res = manager.apply(QStringLiteral("tmo-echo"), QStringLiteral("hi"));
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.output, QStringLiteral("echo:hi"));
}

void TestScripts::skipsOversizedScriptFile()
{
    // Files past kMaxFileBytes (64 kB) are skipped at reload, even when they
    // define a transform — only the size gate decides.
    QByteArray source = "function transform(text) { return text; }\n/*";
    source += QByteArray(64 * 1024, 'x');
    source += "*/\n";
    QVERIFY(source.size() > 64 * 1024);
    writeScript(QStringLiteral("big-file.js"), source);
    ScriptActionManager manager;
    QVERIFY(!manager.hasAction(QStringLiteral("big-file")));
}

void TestScripts::skipsEmptyAndNonJsFiles()
{
    writeScript(QStringLiteral("empty-file.js"), QByteArray());
    writeScript(QStringLiteral("notes.txt"),
                "function transform(text) { return text; }\n");
    ScriptActionManager manager;
    QVERIFY(!manager.hasAction(QStringLiteral("empty-file")));
    QVERIFY(!manager.hasAction(QStringLiteral("notes")));
}

void TestScripts::inputSizeBoundaries()
{
    writeScript(QStringLiteral("bound-echo.js"),
                "function transform(text) { return text; }\n");
    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("bound-echo")));

    // kMaxInputBytes is 256 kB; the gate is strictly-greater, so the exact
    // boundary input is still accepted.
    const QString justUnder = QStringLiteral("a").repeated(256 * 1024 - 1);
    const auto under = manager.apply(QStringLiteral("bound-echo"), justUnder);
    QVERIFY2(under.ok, qPrintable(under.error));
    QCOMPARE(under.output, justUnder);

    const QString exact = QStringLiteral("b").repeated(256 * 1024);
    const auto atEdge = manager.apply(QStringLiteral("bound-echo"), exact);
    QVERIFY2(atEdge.ok, qPrintable(atEdge.error));
    QCOMPARE(atEdge.output, exact);

    const auto over = manager.apply(QStringLiteral("bound-echo"),
                                    QStringLiteral("c").repeated(256 * 1024 + 1));
    QVERIFY(!over.ok);
    QVERIFY(over.error.contains(QStringLiteral("too large")));
}

void TestScripts::emptyInput()
{
    writeScript(QStringLiteral("empty-input.js"),
                "function transform(text) { return text; }\n");
    ScriptActionManager manager;
    const auto res = manager.apply(QStringLiteral("empty-input"), QString());
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.output, QString());
}

void TestScripts::transformNotCallableAtRuntime()
{
    // parseMeta sees the declaration, but the script rebinds the name before
    // apply() reads it: the callable check (not the listing) reports it.
    writeScript(QStringLiteral("not-callable.js"),
                "function transform(text) { return text; }\n"
                "transform = 42;\n");
    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("not-callable")));
    const auto res = manager.apply(QStringLiteral("not-callable"), QStringLiteral("data"));
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("no function transform")));
}

void TestScripts::errorMessagesCarryCallSite()
{
    writeScript(QStringLiteral("err-syntax.js"),
                "function transform(text) { if (true { return text; } }\n");
    writeScript(QStringLiteral("err-runtime.js"),
                "function transform(text) { throw new Error(\"boom\"); }\n");
    ScriptActionManager manager;
    const auto syntax = manager.apply(QStringLiteral("err-syntax"), QStringLiteral("data"));
    QVERIFY(!syntax.ok);
    QVERIFY(syntax.error.startsWith(QStringLiteral("Script error at ")));
    QVERIFY(syntax.error.contains(QStringLiteral("at 1:")));
    const auto runtime = manager.apply(QStringLiteral("err-runtime"), QStringLiteral("data"));
    QVERIFY(!runtime.ok);
    QVERIFY(runtime.error.startsWith(QStringLiteral("transform() error at ")));
    QVERIFY(runtime.error.contains(QStringLiteral("boom")));
}

void TestScripts::exampleTemplateEvaluates()
{
    // The shipped template is not just text: it loads and both of its paths
    // (JSON pretty-print and trim fallback) execute.
    writeScript(QStringLiteral("example-check.js"),
                ScriptActionManager::exampleSource().toUtf8());
    ScriptActionManager manager;
    QVERIFY(manager.hasAction(QStringLiteral("example-check")));
    const auto pretty =
        manager.apply(QStringLiteral("example-check"), QStringLiteral("{\"a\":1}"));
    QVERIFY2(pretty.ok, qPrintable(pretty.error));
    QVERIFY(pretty.output.contains(QStringLiteral("\"a\": 1")));
    const auto trimmed =
        manager.apply(QStringLiteral("example-check"), QStringLiteral("  padded  "));
    QVERIFY2(trimmed.ok, qPrintable(trimmed.error));
    QCOMPARE(trimmed.output, QStringLiteral("padded"));
}

void TestScripts::exportFormPreprocessing()
{
    writeScript(QStringLiteral("exp-default.js"),
                "export default function transform(text) { return \"d:\" + text; }\n");
    writeScript(QStringLiteral("exp-const.js"),
                "export const transform = (text) => \"c:\" + text;\n");
    writeScript(QStringLiteral("let-fn.js"),
                "let transform = function(text) { return \"l:\" + text; };\n");
    ScriptActionManager manager;
    const auto def = manager.apply(QStringLiteral("exp-default"), QStringLiteral("x"));
    QVERIFY2(def.ok, qPrintable(def.error));
    QCOMPARE(def.output, QStringLiteral("d:x"));
    const auto con = manager.apply(QStringLiteral("exp-const"), QStringLiteral("x"));
    QVERIFY2(con.ok, qPrintable(con.error));
    QCOMPARE(con.output, QStringLiteral("c:x"));
    const auto let = manager.apply(QStringLiteral("let-fn"), QStringLiteral("x"));
    QVERIFY2(let.ok, qPrintable(let.error));
    QCOMPARE(let.output, QStringLiteral("l:x"));
}

void TestScripts::metaQuotingAndDefaults()
{
    writeScript(QStringLiteral("meta-single.js"),
                "var meta = { label: 'Single', match: '' };\n"
                "function transform(text) { return text; }\n");
    writeScript(QStringLiteral("meta-absent.js"),
                "function transform(text) { return text; }\n");
    ScriptActionManager manager;
    const auto actions = manager.actions();
    const auto find = [&actions](const QString &id) {
        return std::find_if(actions.begin(), actions.end(),
                            [&id](const ScriptAction &a) { return a.id == id; });
    };
    const auto single = find(QStringLiteral("meta-single"));
    QVERIFY(single != actions.end());
    QCOMPARE(single->label, QStringLiteral("Single"));
    QVERIFY(single->matchPattern.isEmpty()); // empty match: shown for every input
    const auto absent = find(QStringLiteral("meta-absent"));
    QVERIFY(absent != actions.end());
    QVERIFY(absent->matchPattern.isEmpty()); // missing match: shown for every input
}

void TestScripts::applyUsesCachedSourceUntilReload()
{
    // apply() never re-reads the file: edits land only after reload().
    writeScript(QStringLiteral("stale.js"),
                "function transform(text) { return \"v1\"; }\n");
    ScriptActionManager manager;
    const auto first = manager.apply(QStringLiteral("stale"), QStringLiteral("data"));
    QVERIFY2(first.ok, qPrintable(first.error));
    QCOMPARE(first.output, QStringLiteral("v1"));

    writeScript(QStringLiteral("stale.js"),
                "function transform(text) { return \"v2\"; }\n");
    const auto cached = manager.apply(QStringLiteral("stale"), QStringLiteral("data"));
    QVERIFY2(cached.ok, qPrintable(cached.error));
    QCOMPARE(cached.output, QStringLiteral("v1"));

    manager.reload();
    const auto fresh = manager.apply(QStringLiteral("stale"), QStringLiteral("data"));
    QVERIFY2(fresh.ok, qPrintable(fresh.error));
    QCOMPARE(fresh.output, QStringLiteral("v2"));
}

QTEST_GUILESS_MAIN(TestScripts)
#include "tst_scripts.moc"
