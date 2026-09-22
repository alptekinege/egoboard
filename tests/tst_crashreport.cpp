#include <QtTest>

#include "CrashReport.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <csignal>

// Local-first crash reports: schema, redaction, backtrace/ELF parsing and
// bounds — all without ever crashing the production app. The CLI
// (`--crash-report` / `--read-crash-report`) and Settings ▸ Diagnostics drive
// the same CrashReport entry points this file covers, which is the parity.
class TestCrashReport : public QObject
{
    Q_OBJECT

private slots:
    void schemaRoundTripAndForwardCompat();
    void malformedReportsRenderSafely();
    void redactionStripsHomePaths();
    void schemaCarriesNoPayloadFields();
    void signalNamesCoverTheCommonCases();
    void gdbFixtureParsesAndHighlightsFirstEgoboardFrame();
    void gdbGarbageYieldsNoFrames();
    void elfFixtureReportsBuildIdAndSymbols();
    void boundsCapLogsAndFrames();
    void collectWithoutProbesIsHermetic();
    void summaryServesBothCliAndSettings();
    void probesDegradeGracefullyWithoutTools();

private:
    // Minimal ELF64-LE fixture: build-ID note plus, optionally, .debug_info.
    static QByteArray elfFixture(bool withDebug);
    QString writeFixture(const QByteArray &bytes);

    QTemporaryDir m_dir;
};

QByteArray TestCrashReport::elfFixture(bool withDebug)
{
    QByteArray blob;
    const auto appendBytes = [&](const QByteArray &bytes) { blob.append(bytes); };
    const auto appendLe = [&](quint64 value, int size) {
        for (int i = 0; i < size; ++i)
            blob.append(char(value >> (8 * i)));
    };
    const auto appendAt = [&](int offset, quint64 value, int size) {
        for (int i = 0; i < size; ++i)
            blob[int(offset) + i] = char(value >> (8 * i));
    };
    // ELF header (64 bytes); section headers land at shoff, patched below.
    appendBytes(QByteArray::fromHex("7f454c46020101000000000000000000"));
    appendLe(2, 2); // e_type = EXEC
    appendLe(62, 2); // e_machine = x86-64
    appendLe(1, 4); // e_version
    appendLe(0, 8); // e_entry
    appendLe(0, 8); // e_phoff
    const int shoffPos = blob.size();
    appendLe(0, 8); // e_shoff (patched)
    appendLe(0, 4); // e_flags
    appendLe(64, 2); // e_ehsize
    appendLe(0, 2); // e_phentsize
    appendLe(0, 2); // e_phnum
    appendLe(64, 2); // e_shentsize
    const int shnumPos = blob.size();
    appendLe(0, 2); // e_shnum (patched)
    const int shstrndxPos = blob.size();
    appendLe(0, 2); // e_shstrndx (patched)

    // .note.gnu.build-id section: namesz/descsz/type + "GNU\0" + 20 ID bytes.
    const int noteOff = blob.size();
    appendLe(4, 4);
    appendLe(20, 4);
    appendLe(3, 4); // NT_GNU_BUILD_ID
    appendBytes(QByteArray("GNU\0", 4));
    QByteArray buildId;
    for (int i = 1; i <= 20; ++i)
        buildId.append(char(i));
    appendBytes(buildId);
    const int noteSize = blob.size() - noteOff;

    // .debug_info section (4 marker bytes; presence is what matters).
    const int debugOff = blob.size();
    appendBytes(QByteArray("DBG!", 4));

    // .shstrtab: "\0.note.gnu.build-id\0.debug_info\0.shstrtab\0".
    const int strOff = blob.size();
    appendBytes(QByteArray("\0", 1));
    const int noteName = blob.size() - strOff;
    appendBytes(QByteArray(".note.gnu.build-id\0", 20));
    const int debugName = blob.size() - strOff;
    appendBytes(QByteArray(".debug_info\0", 12));
    const int strName = blob.size() - strOff;
    appendBytes(QByteArray(".shstrtab\0", 10));

    // Section headers (null + note + [debug] + strings), 8-aligned.
    while (blob.size() % 8 != 0)
        blob.append(char(0));
    const int shoff = blob.size();
    const auto section = [&](int name, int type, quint64 offset, quint64 size) {
        appendLe(quint64(name), 4);
        appendLe(quint64(type), 4);
        appendLe(0, 8); // sh_flags
        appendLe(0, 8); // sh_addr
        appendLe(offset, 8);
        appendLe(size, 8);
        appendLe(0, 4); // sh_link
        appendLe(0, 4); // sh_info
        appendLe(1, 8); // sh_addralign
        appendLe(0, 8); // sh_entsize
    };
    section(0, 0, 0, 0);
    section(noteName, 7, quint64(noteOff), quint64(noteSize)); // SHT_NOTE
    if (withDebug)
        section(debugName, 1, quint64(debugOff), 4); // SHT_PROGBITS
    const int strIndex = withDebug ? 3 : 2;
    section(strName, 3, quint64(strOff), quint64(blob.size() - strOff)); // SHT_STRTAB
    appendAt(shoffPos, quint64(shoff), 8);
    appendAt(shnumPos, quint64(withDebug ? 4 : 3), 2);
    appendAt(shstrndxPos, quint64(strIndex), 2);
    return blob;
}

QString TestCrashReport::writeFixture(const QByteArray &bytes)
{
    static int counter = 0;
    const QString path = m_dir.filePath(QStringLiteral("elf-fixture-%1").arg(++counter));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(bytes);
    return path;
}

void TestCrashReport::schemaRoundTripAndForwardCompat()
{
    CrashReport::Data data;
    data.appVersion = QStringLiteral("0.1.0-test");
    data.buildId = QStringLiteral("deadbeef");
    data.hasDebugSymbols = true;
    data.signalNumber = SIGSEGV;
    data.signalName = CrashReport::signalNameFor(SIGSEGV);
    CrashReport::Frame frame;
    frame.index = 0;
    frame.function = QStringLiteral("Egoboard::MainWindow::openSettings()");
    frame.file = QStringLiteral("MainWindow.cpp");
    frame.line = 1288;
    frame.isEgoboard = true;
    data.frames.append(frame);
    data.firstEgoboardFrame = 0;
    data.logs << QStringLiteral("egoboard: started");
    data.includedSections << QStringLiteral("Environment");

    const QJsonObject json = CrashReport::toJson(data);
    CrashReport::Data restored;
    QString error;
    QVERIFY2(CrashReport::fromJson(json, &restored, &error), qPrintable(error));
    QCOMPARE(restored.appVersion, QStringLiteral("0.1.0-test"));
    QCOMPARE(restored.buildId, QStringLiteral("deadbeef"));
    QVERIFY(restored.hasDebugSymbols);
    QCOMPARE(restored.frames.size(), 1);
    QCOMPARE(restored.frames.first().function,
             QStringLiteral("Egoboard::MainWindow::openSettings()"));
    QCOMPARE(restored.frames.first().line, 1288);
    QCOMPARE(restored.firstEgoboardFrame, 0);

    // Forward-compatible: unknown keys are ignored on load.
    QJsonObject future = json;
    future.insert(QStringLiteral("newField"), QStringLiteral("new-value"));
    QJsonObject futureFrame;
    futureFrame.insert(QStringLiteral("index"), 1);
    futureFrame.insert(QStringLiteral("futureKey"), 42);
    QJsonArray frames = future.value(QStringLiteral("frames")).toArray();
    frames.append(futureFrame);
    future.insert(QStringLiteral("frames"), frames);
    CrashReport::Data forward;
    QVERIFY(CrashReport::fromJson(future, &forward));
    QCOMPARE(forward.frames.size(), 2);
    QCOMPARE(forward.frames.at(1).function, QStringLiteral("??"));
}

void TestCrashReport::malformedReportsRenderSafely()
{
    // Not an object at all.
    CrashReport::Data data;
    QString error;
    QVERIFY(!CrashReport::fromJson(QJsonObject(), nullptr, &error));
    // Empty object: partial data, still accepted, never crashes the reader.
    QVERIFY(CrashReport::fromJson(QJsonObject(), &data, &error));
    QVERIFY(data.frames.isEmpty());
    QVERIFY(!CrashReport::renderSummary(data).isEmpty());
    // Foreign format tag is rejected with a reason.
    QJsonObject foreign;
    foreign.insert(QStringLiteral("format"), QStringLiteral("other"));
    QVERIFY(!CrashReport::fromJson(foreign, &data, &error));
    QVERIFY(!error.isEmpty());
    // Wrong-typed entries are skipped, the rest survives.
    QJsonObject partial;
    QJsonArray frames;
    frames.append(QStringLiteral("garbage"));
    QJsonObject good;
    good.insert(QStringLiteral("index"), 3);
    good.insert(QStringLiteral("function"), QStringLiteral("main"));
    frames.append(good);
    partial.insert(QStringLiteral("frames"), frames);
    QJsonArray logs;
    logs.append(42);
    logs.append(QStringLiteral("ok line"));
    partial.insert(QStringLiteral("logs"), logs);
    QVERIFY(CrashReport::fromJson(partial, &data));
    QCOMPARE(data.frames.size(), 1);
    QCOMPARE(data.frames.first().function, QStringLiteral("main"));
    QCOMPARE(data.logs, QStringList{QStringLiteral("ok line")});
    QVERIFY(!CrashReport::renderSummary(data).isEmpty());
}

void TestCrashReport::redactionStripsHomePaths()
{
    const QString home = QDir::homePath();
    QVERIFY(!home.isEmpty());
    const QString redacted = CrashReport::redactForSharing(
        QStringLiteral("DB: %1/history.db by alptekin; see /home/other/x and /root/y")
            .arg(home));
    QVERIFY(!redacted.contains(home));
    QVERIFY(redacted.contains(QStringLiteral("~")));
    QVERIFY(!redacted.contains(QStringLiteral("/home/other")));
    QVERIFY(redacted.contains(QStringLiteral("/home/<user>")));
    // Plain text without paths passes through untouched.
    QCOMPARE(CrashReport::redactForSharing(QStringLiteral("egoboard: started")),
             QStringLiteral("egoboard: started"));
}

void TestCrashReport::schemaCarriesNoPayloadFields()
{
    // Allowlist: a future payload/history/key field cannot sneak into the
    // schema without this test naming it first.
    const QJsonObject json = CrashReport::toJson(CrashReport::Data{});
    QSet<QString> keys;
    for (const QString &key : json.keys())
        keys.insert(key);
    const QSet<QString> expected = {
        QStringLiteral("format"), QStringLiteral("version"), QStringLiteral("appVersion"),
        QStringLiteral("buildId"), QStringLiteral("hasDebugSymbols"), QStringLiteral("symbolNote"),
        QStringLiteral("qtVersion"), QStringLiteral("kf6Version"), QStringLiteral("qpa"),
        QStringLiteral("sessionType"), QStringLiteral("sessionDesktop"), QStringLiteral("kernel"),
        QStringLiteral("architecture"), QStringLiteral("executablePath"),
        QStringLiteral("signalNumber"), QStringLiteral("signalName"), QStringLiteral("crashingThread"),
        QStringLiteral("frames"), QStringLiteral("firstEgoboardFrame"),
        QStringLiteral("backtraceNote"), QStringLiteral("backtraceRaw"), QStringLiteral("logs"),
        QStringLiteral("logSource"), QStringLiteral("kwinInfo"), QStringLiteral("tools"),
        QStringLiteral("includedSections"), QStringLiteral("collectedAt"),
    };
    QCOMPARE(keys.size(), expected.size());
    for (const QString &key : expected)
        QVERIFY2(keys.contains(key), qPrintable(key));
}

void TestCrashReport::signalNamesCoverTheCommonCases()
{
    QCOMPARE(CrashReport::signalNameFor(SIGSEGV), QStringLiteral("SIGSEGV"));
    QCOMPARE(CrashReport::signalNameFor(SIGABRT), QStringLiteral("SIGABRT"));
    QCOMPARE(CrashReport::signalNameFor(SIGFPE), QStringLiteral("SIGFPE"));
    QCOMPARE(CrashReport::signalNameFor(0), QStringLiteral("none (live report)"));
    QVERIFY(CrashReport::signalNameFor(99).contains(QStringLiteral("99")));
}

void TestCrashReport::gdbFixtureParsesAndHighlightsFirstEgoboardFrame()
{
    // A realistic `gdb bt` mix: libc frames, an Egoboard frame, an
    // unsymbolized one and a bare address — the fixture never crashes the app.
    const QString raw = QStringLiteral(
        "#0  0x00007f8a1c2425ff in raise (sig=6) at ../sysdeps/unix/sysv/linux/raise.c:51\n"
        "#1  0x0000555555567a10 in Egoboard::MainWindow::openSettings (this=0x5555555) at MainWindow.cpp:1288\n"
        "#2  0x00007f8a1c9abcdf in ?? () from /usr/lib/libQt6Core.so.6\n"
        "#3  0x00005555555abcde\n"
        "garbage line without a frame\n");
    const auto frames = CrashReport::parseGdbBacktrace(raw);
    QCOMPARE(frames.size(), 4);
    QCOMPARE(frames.at(0).function, QStringLiteral("raise"));
    QCOMPARE(frames.at(1).function, QStringLiteral("Egoboard::MainWindow::openSettings"));
    QVERIFY(frames.at(1).isEgoboard);
    QVERIFY(!frames.at(0).isEgoboard);
    QCOMPARE(frames.at(1).file, QStringLiteral("MainWindow.cpp"));
    QCOMPARE(frames.at(1).line, 1288);
    QCOMPARE(frames.at(2).function, QStringLiteral("??"));
    QCOMPARE(frames.at(2).module, QStringLiteral("/usr/lib/libQt6Core.so.6"));
    QCOMPARE(frames.at(3).function, QStringLiteral("??"));
    QCOMPARE(frames.at(3).address, QStringLiteral("0x00005555555abcde"));
    QCOMPARE(CrashReport::firstEgoboardFrameIndex(frames), 1);
}

void TestCrashReport::gdbGarbageYieldsNoFrames()
{
    QVERIFY(CrashReport::parseGdbBacktrace(QString()).isEmpty());
    QVERIFY(CrashReport::parseGdbBacktrace(QStringLiteral("no frames here\nat all\n")).isEmpty());
    QCOMPARE(CrashReport::firstEgoboardFrameIndex({}), -1);
    // A wall of frames is capped, not grown unboundedly.
    QString wall;
    for (int i = 0; i < 200; ++i)
        wall += QStringLiteral("#%1 f() at x.c:1\n").arg(i);
    QCOMPARE(CrashReport::parseGdbBacktrace(wall).size(), CrashReport::kMaxFrames);
}

void TestCrashReport::elfFixtureReportsBuildIdAndSymbols()
{
    const QString withDebug = writeFixture(elfFixture(true));
    const QString stripped = writeFixture(elfFixture(false));
    QVERIFY(!withDebug.isEmpty());
    QVERIFY(!stripped.isEmpty());

    const QString expectedId =
        QString::fromLatin1(QByteArray::fromHex("0102030405060708090a0b0c0d0e0f1011121314").toHex());
    QCOMPARE(CrashReport::readBuildId(withDebug), expectedId);
    QVERIFY(CrashReport::hasDebugSymbols(withDebug));
    QCOMPARE(CrashReport::readBuildId(stripped), expectedId);
    QVERIFY(!CrashReport::hasDebugSymbols(stripped));

    // Truncated and foreign files degrade to empty/false, never crash.
    const QString partialPath = m_dir.filePath(QStringLiteral("elf-partial"));
    QFile partial(partialPath);
    QVERIFY(partial.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QFile source(withDebug);
    QVERIFY(source.open(QIODevice::ReadOnly));
    partial.write(source.read(100));
    QVERIFY(CrashReport::readBuildId(partial.fileName()).isEmpty());
    QVERIFY(!CrashReport::hasDebugSymbols(partial.fileName()));
    const QString junk = writeFixture(QByteArrayLiteral("not an elf at all"));
    QVERIFY(CrashReport::readBuildId(junk).isEmpty());
    QVERIFY(!CrashReport::hasDebugSymbols(junk));
    QVERIFY(CrashReport::readBuildId(QStringLiteral("/does/not/exist")).isEmpty());
}

void TestCrashReport::boundsCapLogsAndFrames()
{
    CrashReport::Env env;
    env.appVersion = QStringLiteral("0.1.0-test");
    env.runToolProbes = false;
    for (int i = 0; i < 500; ++i)
        env.extraLogs.append(QString(500, QLatin1Char('l')) + QString::number(i));
    QString wall;
    for (int i = 0; i < 200; ++i)
        wall += QStringLiteral("#%1 f() at x.c:1\n").arg(i);
    env.gdbBacktraceRaw = wall;
    const CrashReport::Data data = CrashReport::collect(env);
    QVERIFY(data.logs.size() <= CrashReport::kMaxLogLines);
    qint64 bytes = 0;
    for (const QString &line : data.logs)
        bytes += line.toUtf8().size() + 1;
    QVERIFY(bytes <= CrashReport::kMaxLogBytes + 501);
    QVERIFY(data.frames.size() <= CrashReport::kMaxFrames);
    QVERIFY(data.backtraceRaw.toUtf8().size() <= CrashReport::kMaxBacktraceBytes);
}

void TestCrashReport::collectWithoutProbesIsHermetic()
{
    const QString home = QDir::homePath();
    CrashReport::Env env;
    env.appVersion = QStringLiteral("0.1.0-test");
    env.qpa = QStringLiteral("offscreen");
    env.runToolProbes = false;
    env.extraLogs << QStringLiteral("egoboard: watching %1/history.db").arg(home);
    const CrashReport::Data data = CrashReport::collect(env);
    QCOMPARE(data.appVersion, QStringLiteral("0.1.0-test"));
    QCOMPARE(data.qpa, QStringLiteral("offscreen"));
    QVERIFY(!data.qtVersion.isEmpty());
    QVERIFY(!data.collectedAt.isEmpty());
    QVERIFY(data.signalNumber == 0);
    // No probes ran, and the home path in the log line came back redacted.
    QCOMPARE(data.kwinInfo, QStringLiteral("tool probes disabled"));
    QCOMPARE(data.logs.size(), 1);
    QVERIFY(!data.logs.first().contains(home));
    QVERIFY(data.logs.first().contains(QStringLiteral("~")));
    // Collection never carries payload-shaped fields.
    const QJsonObject json = CrashReport::toJson(data);
    QVERIFY(!json.contains(QStringLiteral("entries")));
    QVERIFY(!json.contains(QStringLiteral("clipboard")));
}

void TestCrashReport::summaryServesBothCliAndSettings()
{
    // One renderer feeds `--read-crash-report` and the Settings preview, so a
    // change to either surface shows up in both: pin the shared sections here.
    CrashReport::Env env;
    env.appVersion = QStringLiteral("0.1.0-test");
    env.qpa = QStringLiteral("offscreen");
    env.signalNumber = SIGSEGV;
    env.crashingThread = QStringLiteral("main");
    env.gdbBacktraceRaw = QStringLiteral(
        "#0 raise at raise.c:51\n"
        "#1 Egoboard::TrayController::rebuildMenu at TrayController.cpp:200\n");
    env.runToolProbes = false;
    const CrashReport::Data data = CrashReport::collect(env);
    const QString summary = CrashReport::renderSummary(data);
    QVERIFY(summary.contains(QStringLiteral("0.1.0-test")));
    QVERIFY(summary.contains(QStringLiteral("SIGSEGV")));
    QVERIFY(summary.contains(QStringLiteral("main")));
    QVERIFY(summary.contains(QStringLiteral("=> #1")));
    QVERIFY(summary.contains(QStringLiteral("Privacy:")));
    QVERIFY(summary.contains(QStringLiteral("Tools:")));
    // And the JSON round-trips through the same tolerant reader the Open path uses.
    const QJsonDocument document(CrashReport::toJson(data));
    CrashReport::Data reopened;
    QVERIFY(CrashReport::fromJson(document.object(), &reopened));
    QCOMPARE(CrashReport::renderSummary(reopened), summary);
}

void TestCrashReport::probesDegradeGracefullyWithoutTools()
{
    // Environment-dependent by design: only the completion contract is pinned
    // (valid schema, manual hints present), never a specific tool's presence.
    CrashReport::Env env;
    env.appVersion = QStringLiteral("0.1.0-test");
    env.qpa = QStringLiteral("offscreen");
    env.runToolProbes = true;
    const CrashReport::Data data = CrashReport::collect(env);
    QVERIFY(!data.qtVersion.isEmpty());
    QVERIFY(!data.tools.isEmpty());
    for (const auto &tool : data.tools) {
        QVERIFY(!tool.name.isEmpty());
        QVERIFY(!tool.note.isEmpty()); // manual command/fallback, always
        if (tool.available)
            QVERIFY(tool.note.contains(QStringLiteral("`")));
    }
    QVERIFY(!CrashReport::renderSummary(data).isEmpty());
    QVERIFY(CrashReport::toJson(data).value(QStringLiteral("format")).toString()
            == QStringLiteral("egoboard-crash-report"));
}

QTEST_GUILESS_MAIN(TestCrashReport)
#include "tst_crashreport.moc"
