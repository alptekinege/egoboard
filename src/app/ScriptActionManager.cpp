#include "ScriptActionManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJSValue>
#include <QRegularExpression>
#include <QStandardPaths>

#include <atomic>
#include <chrono>
#include <thread>

namespace {
constexpr int kMaxFileBytes = 64 * 1024;
constexpr int kMaxInputBytes = 256 * 1024;
constexpr int kExecutionTimeoutMs = 2000;
}

ScriptActionManager::ScriptActionManager(QObject *parent)
    : QObject(parent)
{
    reload();
}

QString ScriptActionManager::actionsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // AppDataLocation is ~/.local/share/egoboard
    // Fallback to GenericDataLocation/egoboard if needed
    if (base.isEmpty())
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/egoboard/actions");
    return base + QStringLiteral("/actions");
}

QString ScriptActionManager::exampleSource()
{
    return QStringLiteral(
        "// Egoboard script action — local, offline, sandboxed (QJSEngine)\n"
        "// File: ~/.local/share/egoboard/actions/<name>.js\n"
        "// Required: function transform(text) { return ...; }\n"
        "// Optional: var meta = { label: \"My Action\", match: \"^\\\\s*\\\\{\" };\n"
        "//   match is a JS regex string — empty or missing means always shown.\n"
        "// No file/network access — only pure string transforms.\n"
        "\n"
        "function transform(text) {\n"
        "  // Example: pretty-print JSON if possible, else return trimmed\n"
        "  try {\n"
        "    const obj = JSON.parse(text);\n"
        "    return JSON.stringify(obj, null, 2);\n"
        "  } catch (e) {\n"
        "    return text.trim();\n"
        "  }\n"
        "}\n"
        "\n"
        "var meta = {\n"
        "  label: \"Pretty JSON\",\n"
        "  match: \"^\\\\s*[\\\\{\\\\[]\"\n"
        "};\n");
}

QString ScriptActionManager::preprocessSource(const QString &source)
{
    QString out = source;
    // Strip ES module `export` so QJSEngine can evaluate the file.
    // Handles: export function, export const/var/let, export default function, export { ... }
    static const QRegularExpression exportDefaultRe(QStringLiteral(R"(^\s*export\s+default\s+)"), QRegularExpression::MultilineOption);
    static const QRegularExpression exportRe(QStringLiteral(R"(^\s*export\s+)"), QRegularExpression::MultilineOption);
    out.remove(exportDefaultRe);
    out.remove(exportRe);
    // Remove `export { ... };` lines (module re-exports not needed)
    static const QRegularExpression exportBlockRe(QStringLiteral(R"(^\s*export\s*\{[^}]*\}\s*;?\s*$)"), QRegularExpression::MultilineOption);
    out.remove(exportBlockRe);
    // `const transform = ...` / `let transform = ...` are lexically scoped to
    // the evaluated script and never appear on the global object; promote them
    // to `var` so arrow-function transforms are callable.
    static const QRegularExpression lexicalTransformRe(
        QStringLiteral(R"(^(\s*)(?:const|let)(\s+transform\s*=))"),
        QRegularExpression::MultilineOption);
    out.replace(lexicalTransformRe, QStringLiteral("\\1var\\2"));
    return out;
}

ScriptAction ScriptActionManager::parseMeta(const QString &id, const QString &filePath, const QString &source)
{
    ScriptAction a;
    a.id = id;
    a.filePath = filePath;
    a.label = id;
    // Function declarations, function expressions and arrow functions.
    static const QRegularExpression transformRe(QStringLiteral(
        R"((function\s+transform\s*\()|(\btransform\s*=\s*(?:function\b|async\s+function\b|\([^)]*\)\s*=>|[A-Za-z_$][\w$]*\s*=>)))"));
    a.hasTransform = transformRe.match(source).hasMatch();

    // Quick parse of meta.label and meta.match without executing JS (fast, no engine)
    // Supports: var meta = { label: "X", match: "Y" } or const meta = { ... }
    static const QRegularExpression labelRe(QStringLiteral(R"(meta\s*=\s*\{[^}]*label\s*:\s*[\"']([^\"']+)[\"'])"));
    static const QRegularExpression matchRe(QStringLiteral(R"(meta\s*=\s*\{[^}]*match\s*:\s*[\"']([^\"']*)[\"'])"));
    // Also support regex literal: match: /^\s*\{/
    static const QRegularExpression matchRegexRe(QStringLiteral(R"(meta\s*=\s*\{[^}]*match\s*:\s*/((?:\\/|[^/])+)/)"));

    auto lm = labelRe.match(source);
    if (lm.hasMatch()) {
        a.label = lm.captured(1);
    } else {
        // Capitalize id for fallback: my-action -> My Action
        QString pretty = id;
        pretty.replace(QLatin1Char('-'), QLatin1Char(' '));
        pretty.replace(QLatin1Char('_'), QLatin1Char(' '));
        bool cap = true;
        for (int i = 0; i < pretty.size(); ++i) {
            if (cap && pretty.at(i).isLetter()) { pretty[i] = pretty.at(i).toUpper(); cap = false; }
            else if (pretty.at(i).isSpace()) cap = true;
            else pretty[i] = pretty.at(i).toLower();
        }
        a.label = pretty;
        if (a.label.isEmpty()) a.label = id;
    }
    auto mm = matchRe.match(source);
    if (mm.hasMatch()) {
        a.matchPattern = mm.captured(1);
    } else {
        auto mr = matchRegexRe.match(source);
        if (mr.hasMatch()) a.matchPattern = mr.captured(1);
    }
    return a;
}

void ScriptActionManager::reload()
{
    m_actions.clear();
    const QDir dir(actionsDir());
    if (!dir.exists()) {
        QDir().mkpath(dir.absolutePath());
        return;
    }
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.js"), QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (fi.size() > kMaxFileBytes) continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString source = QString::fromUtf8(f.readAll());
        f.close();
        if (source.trimmed().isEmpty()) continue;
        const QString id = fi.completeBaseName();
        ScriptAction a = parseMeta(id, fi.absoluteFilePath(), source);
        // Only list files that at least define transform (otherwise not an action)
        if (!a.hasTransform) continue;
        a.source = preprocessSource(source);
        m_actions.append(a);
    }
}

bool ScriptActionManager::hasAction(const QString &id) const
{
    for (const auto &a : m_actions) if (a.id == id) return true;
    return false;
}

ScriptActionManager::Result ScriptActionManager::apply(const QString &id, const QString &input) const
{
    if (input.size() > kMaxInputBytes) {
        return {false, {}, QObject::tr("Input too large for script transform (>256 kB)")};
    }
    const ScriptAction *action = nullptr;
    for (const auto &a : m_actions) {
        if (a.id == id) { action = &a; break; }
    }
    if (!action) {
        return {false, {}, QObject::tr("Script not found: %1").arg(id)};
    }

    QJSEngine engine;
    // No exposure of file/network APIs — engine starts clean.

    // Watchdog: QJSEngine::setInterrupted() is safe to call from another
    // thread, so a runaway loop (while (true) {}) cannot freeze the GUI.
    std::atomic_bool interrupted{false};
    std::atomic_bool finished{false};
    std::thread watchdog([&engine, &interrupted, &finished] {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kExecutionTimeoutMs);
        while (!finished.load(std::memory_order_relaxed)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                interrupted.store(true, std::memory_order_relaxed);
                engine.setInterrupted(true);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    const auto stopWatchdog = [&] {
        finished.store(true, std::memory_order_relaxed);
        if (watchdog.joinable())
            watchdog.join();
    };
    const auto timedOut = [&]() -> Result {
        stopWatchdog();
        return {false, {}, QObject::tr("Script timed out after %1 ms").arg(kExecutionTimeoutMs)};
    };

    const QJSValue evalRes = engine.evaluate(action->source, action->filePath);
    if (interrupted.load(std::memory_order_relaxed))
        return timedOut();
    if (evalRes.isError()) {
        stopWatchdog();
        return {false, {}, QObject::tr("Script error at %1: %2").arg(evalRes.property(QStringLiteral("lineNumber")).toString(), evalRes.toString())};
    }
    QJSValue fn = engine.globalObject().property(QStringLiteral("transform"));
    if (!fn.isCallable()) {
        stopWatchdog();
        return {false, {}, QObject::tr("Script has no function transform(text)")};
    }
    const QJSValueList args = { QJSValue(input) };
    const QJSValue res = fn.call(args);
    if (interrupted.load(std::memory_order_relaxed))
        return timedOut();
    if (res.isError()) {
        stopWatchdog();
        return {false, {}, QObject::tr("transform() error at %1: %2").arg(res.property(QStringLiteral("lineNumber")).toString(), res.toString())};
    }
    stopWatchdog();
    if (res.isUndefined() || res.isNull())
        return {true, {}, {}};
    return {true, res.toString(), {}};
}
