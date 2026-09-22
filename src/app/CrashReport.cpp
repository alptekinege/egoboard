#include "CrashReport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#include <csignal>

#if __has_include(<kconfig_version.h>)
#include <kconfig_version.h>
#endif

namespace CrashReport {
namespace {

// --- small process helper ----------------------------------------------------

// Runs `program args` with a tight timeout; empty output on any failure
// (missing binary, timeout, crash). Never throws, never blocks the caller
// longer than timeoutMs.
QString runTool(const QString &program, const QStringList &args, int timeoutMs,
                bool *started = nullptr)
{
    const QString bin = QStandardPaths::findExecutable(program);
    if (bin.isEmpty()) {
        if (started)
            *started = false;
        return {};
    }
    QProcess process;
    process.start(bin, args);
    if (!process.waitForStarted(timeoutMs)) {
        if (started)
            *started = false;
        return {};
    }
    if (started)
        *started = true;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        return {};
    }
    QByteArray out = process.readAllStandardOutput();
    if (out.size() > kMaxToolOutputBytes)
        out = out.left(int(kMaxToolOutputBytes));
    return QString::fromUtf8(out);
}

QString firstLine(const QString &text)
{
    return text.split(QLatin1Char('\n')).value(0).trimmed();
}

// --- bounds -------------------------------------------------------------------

QStringList capLines(const QStringList &lines)
{
    QStringList capped = lines.mid(0, kMaxLogLines);
    qint64 bytes = 0;
    for (int i = 0; i < capped.size(); ++i) {
        bytes += capped.at(i).toUtf8().size() + 1;
        if (bytes > kMaxLogBytes) {
            capped = capped.mid(0, i + 1);
            break;
        }
    }
    return capped;
}

// --- ELF walk (build ID + .debug_info) ------------------------------------------

struct ElfProbe {
    bool valid = false;
    QString buildId;
    bool debug = false;
};

// Reads are bounds-checked against fileSize with quint64 arithmetic; any
// anomaly yields an invalid probe instead of a crash or an over-read.
ElfProbe probeElf(const QString &path)
{
    ElfProbe probe;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return probe;
    const qint64 fileSize = file.size();
    if (fileSize < 64 || fileSize > 256 * 1024 * 1024)
        return probe;
    const QByteArray blob = file.readAll();
    const auto u8 = [&](quint64 offset) -> quint64 {
        if (offset >= quint64(blob.size()))
            return 0;
        return quint64(quint8(blob.at(int(offset))));
    };
    const auto bytes = [&](quint64 offset, int count, bool littleEndian) -> quint64 {
        if (count <= 0 || count > 8 || offset > quint64(blob.size())
            || quint64(count) > quint64(blob.size()) - offset)
            return 0;
        quint64 value = 0;
        for (int i = 0; i < count; ++i) {
            const quint64 byte = u8(offset + quint64(i));
            value |= littleEndian ? (byte << (8 * i)) : ((value << 8) | byte);
        }
        return value;
    };
    if (!(u8(0) == 0x7f && u8(1) == 'E' && u8(2) == 'L' && u8(3) == 'F'))
        return probe;
    const bool is64 = u8(4) == 2;
    if (!is64 && u8(4) != 1)
        return probe;
    const bool littleEndian = u8(5) != 2; // ELFDATA2MSB = 2
    const quint64 shoff = bytes(is64 ? 0x28 : 0x20, is64 ? 8 : 4, littleEndian);
    const quint64 shentsize = bytes(is64 ? 0x3a : 0x2e, 2, littleEndian);
    quint64 shnum = bytes(is64 ? 0x3c : 0x30, 2, littleEndian);
    const quint64 shstrndx = bytes(is64 ? 0x3e : 0x32, 2, littleEndian);
    if (shentsize < (is64 ? 64 : 40) || shnum == 0 || shnum > 4096 || shoff == 0)
        return probe;
    const quint64 fileSizeU = quint64(fileSize);
    if (shoff >= fileSizeU)
        return probe;
    // shnum == 0 with sh_size overflow (PN_XNUM) is out of scope: capped above.
    const auto sectionField = [&](quint64 index, quint64 fieldOffset, int size) -> quint64 {
        const quint64 entry = shoff + index * shentsize + fieldOffset;
        if (entry >= fileSizeU || quint64(size) > fileSizeU - entry)
            return 0;
        return bytes(entry, size, littleEndian);
    };
    const auto sectionName = [&](quint64 strtab, quint64 nameOffset) -> QByteArray {
        if (strtab >= fileSizeU || nameOffset >= fileSizeU - strtab)
            return {};
        quint64 end = nameOffset;
        while (strtab + end < fileSizeU && end - nameOffset < 256 && blob.at(int(strtab + end)) != '\0')
            ++end;
        return blob.mid(int(strtab + nameOffset), int(end - nameOffset));
    };
    // shstrtab location first (name field at +0, type at +4, offset/size depend).
    const quint64 nameOff = 0, typeOff = 4;
    const quint64 dataOffField = is64 ? 24 : 16, sizeField = is64 ? 32 : 20;
    const int addrSize = is64 ? 8 : 4;
    if (shstrndx >= shnum)
        return probe;
    const quint64 strtab = sectionField(shstrndx, dataOffField, addrSize);
    const quint64 strtabSize = sectionField(shstrndx, sizeField, addrSize);
    if (strtab >= fileSizeU || strtabSize > fileSizeU - strtab)
        return probe;
    probe.valid = true;
    for (quint64 i = 1; i < shnum; ++i) {
        const quint64 type = sectionField(i, typeOff, 4);
        if (type == 0 && sectionField(i, nameOff, 4) == 0 && sectionField(i, dataOffField, addrSize) == 0
            && sectionField(i, sizeField, addrSize) == 0)
            continue;
        const QByteArray name = sectionName(strtab, sectionField(i, nameOff, 4));
        const quint64 dataOff = sectionField(i, dataOffField, addrSize);
        const quint64 dataSize = sectionField(i, sizeField, addrSize);
        if (dataOff >= fileSizeU || dataSize > fileSizeU - dataOff)
            continue;
        if (type == 1 && name == ".debug_info") // SHT_PROGBITS
            probe.debug = true;
        if (type == 7 && name == ".note.gnu.build-id") { // SHT_NOTE
            // namesz(4) descsz(4) type(4) name((namesz+3)&~3) desc…
            const quint64 namesz = bytes(dataOff, 4, littleEndian);
            const quint64 descsz = bytes(dataOff + 4, 4, littleEndian);
            const quint64 noteType = bytes(dataOff + 8, 4, littleEndian);
            if (namesz == 4 && noteType == 3 && descsz >= 4 && descsz <= 64) { // NT_GNU_BUILD_ID
                const quint64 descOff = dataOff + 12 + ((namesz + 3) & ~quint64(3));
                if (descOff < fileSizeU && descsz <= fileSizeU - descOff && descOff + descsz <= dataOff + dataSize) {
                    probe.buildId = QString::fromLatin1(
                        blob.mid(int(descOff), int(descsz)).toHex());
                }
            }
        }
    }
    return probe;
}

// --- gdb backtrace grammar -------------------------------------------------------

bool looksLikeEgoboardFrame(const QString &function, const QString &module)
{
    return function.contains(QStringLiteral("Egoboard"), Qt::CaseInsensitive)
        || module.contains(QStringLiteral("egoboard"), Qt::CaseInsensitive);
}

} // namespace

QString signalNameFor(int signalNumber)
{
    switch (signalNumber) {
    case SIGSEGV:
        return QStringLiteral("SIGSEGV");
    case SIGABRT:
        return QStringLiteral("SIGABRT");
    case SIGFPE:
        return QStringLiteral("SIGFPE");
    case SIGILL:
        return QStringLiteral("SIGILL");
    case SIGBUS:
        return QStringLiteral("SIGBUS");
    case SIGTERM:
        return QStringLiteral("SIGTERM");
    case 0:
        return QStringLiteral("none (live report)");
    default:
        break;
    }
    return QStringLiteral("signal %1").arg(signalNumber);
}

QString redactForSharing(const QString &text)
{
    QString out = text;
    const QString home = QDir::homePath();
    if (!home.isEmpty() && home != QStringLiteral("/"))
        out.replace(home, QStringLiteral("~"));
    // Other users' homes (log excerpts may quote them) collapse to a placeholder.
    static const QRegularExpression homeUser(QStringLiteral("/home/[^/\\s:\"']+"));
    out.replace(homeUser, QStringLiteral("/home/<user>"));
    // A bare /root outside the reporter's own home gets the same treatment.
    static const QRegularExpression rootDir(QStringLiteral("(?<![\\w])/root(?![\\w])"));
    out.replace(rootDir, QStringLiteral("~"));
    return out;
}

QVector<Frame> parseGdbBacktrace(const QString &text)
{
    QVector<Frame> frames;
    const QStringView body(text);
    // Per-line parse; anything unrecognized is skipped, never fatal.
    static const QRegularExpression frameLine(
        QStringLiteral("^#(\\d+)\\s+(?:0x[0-9a-fA-F]+\\s+in\\s+)?(\\S.*?)(?:\\s+\\((.*)\\))?"
                       "(?:\\s+(?:at|from)\\s+(\\S.*?))?\\s*$"));
    for (const QStringView rawView : body.split(QLatin1Char('\n'))) {
        if (frames.size() >= kMaxFrames)
            break;
        const QString line = rawView.trimmed().toString();
        if (!line.startsWith(QLatin1Char('#')))
            continue;
        const auto match = frameLine.match(line);
        if (!match.hasMatch())
            continue;
        Frame frame;
        frame.index = match.captured(1).toInt();
        frame.function = match.captured(2).trimmed();
        QString location = match.captured(4).trimmed();
        // Bare address with no symbol ("#3 0x000055..."): keep it as the
        // address instead of a bogus function name.
        if (frame.function.startsWith(QStringLiteral("0x"))
            && match.captured(3).isEmpty() && location.isEmpty()) {
            frame.address = frame.function;
            frame.function = QStringLiteral("??");
        }
        // "from /path/lib.so" marks a module; "at file:line" a position.
        if (line.contains(QStringLiteral(" from ")) && !location.isEmpty()
            && !location.contains(QLatin1Char(':'))) {
            frame.module = location;
        } else if (!location.isEmpty()) {
            const int colon = location.lastIndexOf(QLatin1Char(':'));
            if (colon > 0) {
                frame.file = location.left(colon);
                frame.line = location.mid(colon + 1).toInt();
            } else {
                frame.file = location;
            }
        }
        if (frame.function.isEmpty())
            frame.function = QStringLiteral("??");
        frame.isEgoboard = looksLikeEgoboardFrame(frame.function, frame.module);
        frames.append(frame);
    }
    return frames;
}

int firstEgoboardFrameIndex(const QVector<Frame> &frames)
{
    for (int i = 0; i < frames.size(); ++i) {
        if (frames.at(i).isEgoboard)
            return i;
    }
    return -1;
}

QString readBuildId(const QString &executablePath)
{
    return probeElf(executablePath).buildId;
}

bool hasDebugSymbols(const QString &executablePath)
{
    const ElfProbe probe = probeElf(executablePath);
    return probe.valid && probe.debug;
}

Data collect(const Env &env)
{
    Data data;
    data.appVersion = env.appVersion;
    data.qtVersion = QString::fromUtf8(qVersion());
#if defined(KCONFIG_VERSION_STRING)
    data.kf6Version = QString::fromUtf8(KCONFIG_VERSION_STRING);
#else
    data.kf6Version = QStringLiteral("unknown");
#endif
    if (!env.kf6Version.trimmed().isEmpty())
        data.kf6Version = env.kf6Version.trimmed();
    data.qpa = env.qpa.trimmed().isEmpty() ? QStringLiteral("unknown") : env.qpa.trimmed();
    data.sessionType = QString::fromUtf8(qgetenv("XDG_SESSION_TYPE")).trimmed();
    if (data.sessionType.isEmpty())
        data.sessionType = QStringLiteral("unknown");
    data.sessionDesktop = QString::fromUtf8(qgetenv("XDG_SESSION_DESKTOP")).trimmed();
    if (data.sessionDesktop.isEmpty())
        data.sessionDesktop = QString::fromUtf8(qgetenv("XDG_CURRENT_DESKTOP")).trimmed();
    if (data.sessionDesktop.isEmpty())
        data.sessionDesktop = QStringLiteral("unknown");
    data.kernel = QSysInfo::kernelVersion();
    data.architecture = QSysInfo::currentCpuArchitecture();
    const QString exe = env.executablePath.trimmed().isEmpty()
        ? QCoreApplication::applicationFilePath()
        : env.executablePath.trimmed();
    const ElfProbe elf = probeElf(exe);
    data.buildId = elf.buildId;
    data.hasDebugSymbols = elf.valid && elf.debug;
    data.symbolNote = data.hasDebugSymbols
        ? QStringLiteral("Debug symbols are present; traces should resolve.")
        : QStringLiteral("No debug symbols found. Rebuild RelWithDebInfo (or install the "
                         "matching -dbg symbols) for symbolized traces; quote the build ID above.");
    data.executablePath = exe;
    data.signalNumber = env.signalNumber;
    data.signalName = signalNameFor(env.signalNumber);
    data.crashingThread = env.crashingThread.trimmed();
    data.collectedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    data.includedSections << QStringLiteral("Environment") << QStringLiteral("Versions");

    // Backtrace: pre-captured text only (collect() never crashes anything).
    if (!env.gdbBacktraceRaw.trimmed().isEmpty()) {
        QString raw = env.gdbBacktraceRaw;
        if (raw.toUtf8().size() > kMaxBacktraceBytes)
            raw = raw.left(int(kMaxBacktraceBytes));
        data.backtraceRaw = redactForSharing(raw);
        data.frames = parseGdbBacktrace(raw);
        data.firstEgoboardFrame = firstEgoboardFrameIndex(data.frames);
        data.backtraceNote = data.frames.isEmpty()
            ? QStringLiteral("Backtrace text was provided but no frames parsed; raw kept below.")
            : QStringLiteral("Parsed %1 frame(s) from the provided backtrace.").arg(data.frames.size());
        data.includedSections << QStringLiteral("Backtrace");
    } else {
        data.backtraceNote = env.signalNumber == 0
            ? QStringLiteral("Live report: no crash was captured, no backtrace attached. If the "
                             "app crashes, locate the dump with `coredumpctl list egoboard` and "
                             "attach its `gdb bt` output.")
            : QStringLiteral("No backtrace was captured for this crash (see Tools for how to "
                             "extract one from the dump).");
    }

    // Logs: caller lines first, then the user journal when probes are on.
    QStringList logs;
    for (const QString &line : env.extraLogs)
        logs.append(line);
    data.logSource = QStringLiteral("in-app lines only");
    if (env.runToolProbes) {
        bool started = false;
        const QString journal = runTool(
            QStringLiteral("journalctl"),
            {QStringLiteral("--user"), QStringLiteral("--no-pager"), QStringLiteral("-n"),
             QString::number(kMaxLogLines), QStringLiteral("-o"), QStringLiteral("short")},
            2000, &started);
        if (started) {
            int kept = 0;
            for (const QString &line : journal.split(QLatin1Char('\n'))) {
                if (line.contains(QStringLiteral("egoboard"), Qt::CaseInsensitive)) {
                    logs.append(line);
                    ++kept;
                }
            }
            data.logSource = kept > 0
                ? QStringLiteral("user journal (egoboard lines, last %1)").arg(kMaxLogLines)
                : QStringLiteral("user journal had no egoboard lines; in-app lines only");
        } else {
            data.logSource =
                QStringLiteral("journalctl unavailable — run `journalctl --user -n 200 | grep -i "
                               "egoboard` manually");
        }
        // KWin one-liner for the Wayland/X11 context.
        bool kwinStarted = false;
        QString kwin = runTool(QStringLiteral("kwin_wayland"), {QStringLiteral("--version")}, 800,
                               &kwinStarted);
        if (!kwinStarted)
            kwin = runTool(QStringLiteral("kwin_x11"), {QStringLiteral("--version")}, 800,
                           &kwinStarted);
        data.kwinInfo = kwinStarted && !firstLine(kwin).isEmpty()
            ? firstLine(kwin)
            : QStringLiteral("KWin version probe unavailable");
    } else {
        data.kwinInfo = QStringLiteral("tool probes disabled");
    }
    data.logs = capLines(logs);
    data.includedSections << QStringLiteral("Logs");

    // Tool inventory with manual fallbacks (never requires root).
    if (env.runToolProbes) {
        const auto probeTool = [](const QString &name, const QStringList &versionArgs,
                                  const QString &manual) {
            ToolInfo tool;
            tool.name = name;
            bool started = false;
            const QString out = runTool(name, versionArgs, 1500, &started);
            tool.available = started;
            tool.version = started ? firstLine(out) : QString();
            tool.note = manual;
            return tool;
        };
        data.tools.append(
            probeTool(QStringLiteral("coredumpctl"), {QStringLiteral("--version")},
                      QStringLiteral("Locate dumps: `coredumpctl list egoboard`. Extract a trace: "
                                     "`coredumpctl debug egoboard` (needs gdb + symbols).")));
        // Latest dump summary when the tool exists (locate, don't extract).
        if (data.tools.last().available) {
            bool started = false;
            const QString list = runTool(
                QStringLiteral("coredumpctl"),
                {QStringLiteral("--no-pager"), QStringLiteral("--no-legend"),
                 QStringLiteral("list"), QStringLiteral("egoboard")},
                2500, &started);
            const QStringList rows = list.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (started && !rows.isEmpty())
                data.tools.last().note += QStringLiteral(" Latest: ") + rows.last().trimmed().left(160);
            else if (started)
                data.tools.last().note += QStringLiteral(" No dumps stored.");
        }
        data.tools.append(probeTool(QStringLiteral("journalctl"), {QStringLiteral("--version")},
                                    QStringLiteral("Scoped logs: `journalctl --user -n 200 | grep -i "
                                                   "egoboard`. No root needed.")));
        data.tools.append(probeTool(
            QStringLiteral("gdb"), {QStringLiteral("--version")},
            QStringLiteral("Symbolize: `gdb -batch -ex bt ./egoboard core`. Install gdb when missing.")));
        data.tools.append(
            probeTool(QStringLiteral("addr2line"), {QStringLiteral("--version")},
                      QStringLiteral("Resolve one address: `addr2line -e <exe> -f -C <addr>`.")));
        data.includedSections << QStringLiteral("Tools");
    } else {
        data.tools.append(ToolInfo{QStringLiteral("coredumpctl"), false, QString(),
                                   QStringLiteral("probes disabled")});
        data.includedSections << QStringLiteral("Tools (probes disabled)");
    }

    // Redact everything user-visible/serializable at the end, in one place.
    data.executablePath = redactForSharing(data.executablePath);
    data.kwinInfo = redactForSharing(data.kwinInfo);
    for (QString &line : data.logs)
        line = redactForSharing(line);
    data.logSource = redactForSharing(data.logSource);
    for (ToolInfo &tool : data.tools) {
        tool.version = redactForSharing(tool.version);
        tool.note = redactForSharing(tool.note);
    }
    return data;
}

QJsonObject toJson(const Data &data)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), data.formatTag);
    root.insert(QStringLiteral("version"), data.formatVersion);
    root.insert(QStringLiteral("appVersion"), data.appVersion);
    root.insert(QStringLiteral("buildId"), data.buildId);
    root.insert(QStringLiteral("hasDebugSymbols"), data.hasDebugSymbols);
    root.insert(QStringLiteral("symbolNote"), data.symbolNote);
    root.insert(QStringLiteral("qtVersion"), data.qtVersion);
    root.insert(QStringLiteral("kf6Version"), data.kf6Version);
    root.insert(QStringLiteral("qpa"), data.qpa);
    root.insert(QStringLiteral("sessionType"), data.sessionType);
    root.insert(QStringLiteral("sessionDesktop"), data.sessionDesktop);
    root.insert(QStringLiteral("kernel"), data.kernel);
    root.insert(QStringLiteral("architecture"), data.architecture);
    root.insert(QStringLiteral("executablePath"), data.executablePath);
    root.insert(QStringLiteral("signalNumber"), data.signalNumber);
    root.insert(QStringLiteral("signalName"), data.signalName);
    root.insert(QStringLiteral("crashingThread"), data.crashingThread);
    QJsonArray frames;
    for (const Frame &frame : data.frames) {
        QJsonObject entry;
        entry.insert(QStringLiteral("index"), frame.index);
        entry.insert(QStringLiteral("function"), frame.function);
        entry.insert(QStringLiteral("file"), frame.file);
        entry.insert(QStringLiteral("line"), frame.line);
        entry.insert(QStringLiteral("address"), frame.address);
        entry.insert(QStringLiteral("module"), frame.module);
        entry.insert(QStringLiteral("isEgoboard"), frame.isEgoboard);
        frames.append(entry);
    }
    root.insert(QStringLiteral("frames"), frames);
    root.insert(QStringLiteral("firstEgoboardFrame"), data.firstEgoboardFrame);
    root.insert(QStringLiteral("backtraceNote"), data.backtraceNote);
    root.insert(QStringLiteral("backtraceRaw"), data.backtraceRaw);
    QJsonArray logs;
    for (const QString &line : data.logs)
        logs.append(line);
    root.insert(QStringLiteral("logs"), logs);
    root.insert(QStringLiteral("logSource"), data.logSource);
    root.insert(QStringLiteral("kwinInfo"), data.kwinInfo);
    QJsonArray tools;
    for (const ToolInfo &tool : data.tools) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), tool.name);
        entry.insert(QStringLiteral("available"), tool.available);
        entry.insert(QStringLiteral("version"), tool.version);
        entry.insert(QStringLiteral("note"), tool.note);
        tools.append(entry);
    }
    root.insert(QStringLiteral("tools"), tools);
    QJsonArray sections;
    for (const QString &section : data.includedSections)
        sections.append(section);
    root.insert(QStringLiteral("includedSections"), sections);
    root.insert(QStringLiteral("collectedAt"), data.collectedAt);
    return root;
}

bool fromJson(const QJsonObject &object, Data *data, QString *error)
{
    if (!data)
        return false;
    *data = Data{};
    const QString tag = object.value(QStringLiteral("format")).toString();
    if (!tag.isEmpty() && tag != data->formatTag) {
        if (error)
            *error = QStringLiteral("Not an Egoboard crash report (format=%1).").arg(tag);
        return false;
    }
    const auto stringValue = [&](const char *key) {
        return object.value(QLatin1String(key)).toString();
    };
    data->formatTag = tag.isEmpty() ? data->formatTag : tag;
    data->formatVersion = object.value(QStringLiteral("version")).toInt(1);
    data->appVersion = stringValue("appVersion");
    data->buildId = stringValue("buildId");
    data->hasDebugSymbols = object.value(QStringLiteral("hasDebugSymbols")).toBool(false);
    data->symbolNote = stringValue("symbolNote");
    data->qtVersion = stringValue("qtVersion");
    data->kf6Version = stringValue("kf6Version");
    data->qpa = stringValue("qpa");
    data->sessionType = stringValue("sessionType");
    data->sessionDesktop = stringValue("sessionDesktop");
    data->kernel = stringValue("kernel");
    data->architecture = stringValue("architecture");
    data->executablePath = stringValue("executablePath");
    data->signalNumber = object.value(QStringLiteral("signalNumber")).toInt(0);
    data->signalName = stringValue("signalName");
    data->crashingThread = stringValue("crashingThread");
    const QJsonArray frames = object.value(QStringLiteral("frames")).toArray();
    for (const auto &value : frames) {
        if (!value.isObject())
            continue; // malformed entry: skip, keep the rest
        if (data->frames.size() >= kMaxFrames)
            break;
        const QJsonObject entry = value.toObject();
        Frame frame;
        frame.index = entry.value(QStringLiteral("index")).toInt(-1);
        frame.function = entry.value(QStringLiteral("function")).toString();
        frame.file = entry.value(QStringLiteral("file")).toString();
        frame.line = entry.value(QStringLiteral("line")).toInt(-1);
        frame.address = entry.value(QStringLiteral("address")).toString();
        frame.module = entry.value(QStringLiteral("module")).toString();
        frame.isEgoboard = entry.value(QStringLiteral("isEgoboard")).toBool(false);
        if (frame.function.isEmpty())
            frame.function = QStringLiteral("??");
        data->frames.append(frame);
    }
    data->firstEgoboardFrame = object.value(QStringLiteral("firstEgoboardFrame")).toInt(-1);
    if (data->firstEgoboardFrame < 0)
        data->firstEgoboardFrame = firstEgoboardFrameIndex(data->frames);
    data->backtraceNote = stringValue("backtraceNote");
    data->backtraceRaw = stringValue("backtraceRaw");
    const QJsonArray logs = object.value(QStringLiteral("logs")).toArray();
    for (const auto &value : logs) {
        if (data->logs.size() >= kMaxLogLines)
            break;
        const QString line = value.toString();
        if (!line.isNull())
            data->logs.append(line);
    }
    data->logSource = stringValue("logSource");
    data->kwinInfo = stringValue("kwinInfo");
    const QJsonArray tools = object.value(QStringLiteral("tools")).toArray();
    for (const auto &value : tools) {
        if (!value.isObject())
            continue;
        const QJsonObject entry = value.toObject();
        ToolInfo tool;
        tool.name = entry.value(QStringLiteral("name")).toString();
        tool.available = entry.value(QStringLiteral("available")).toBool(false);
        tool.version = entry.value(QStringLiteral("version")).toString();
        tool.note = entry.value(QStringLiteral("note")).toString();
        data->tools.append(tool);
    }
    const QJsonArray sections = object.value(QStringLiteral("includedSections")).toArray();
    for (const auto &value : sections) {
        const QString section = value.toString();
        if (!section.isEmpty())
            data->includedSections.append(section);
    }
    data->collectedAt = stringValue("collectedAt");
    return true;
}

QString renderSummary(const Data &data)
{
    QString out;
    out += QStringLiteral("Egoboard crash report (schema v%1, collected %2)\n")
               .arg(data.formatVersion)
               .arg(data.collectedAt.isEmpty() ? QStringLiteral("unknown time") : data.collectedAt);
    out += QStringLiteral("App: %1  Build ID: %2\n")
               .arg(data.appVersion.isEmpty() ? QStringLiteral("unknown") : data.appVersion,
                    data.buildId.isEmpty() ? QStringLiteral("unreadable") : data.buildId);
    out += QStringLiteral("Symbols: %1 — %2\n")
               .arg(data.hasDebugSymbols ? QStringLiteral("present") : QStringLiteral("stripped"),
                    data.symbolNote);
    out += QStringLiteral("Qt: %1  KF6: %2  QPA: %3 (%4 / %5)\n")
               .arg(data.qtVersion, data.kf6Version, data.qpa, data.sessionType, data.sessionDesktop);
    out += QStringLiteral("OS: %1 (%2)  Exe: %3\n")
               .arg(data.kernel, data.architecture, data.executablePath);
    if (data.signalNumber == 0)
        out += QStringLiteral("Signal: none (live report — no crash captured)\n");
    else
        out += QStringLiteral("Signal: %1 (%2)%3\n")
                   .arg(data.signalName)
                   .arg(data.signalNumber)
                   .arg(data.crashingThread.isEmpty()
                            ? QString()
                            : QStringLiteral(" on thread '%1'").arg(data.crashingThread));
    if (!data.frames.isEmpty()) {
        out += QStringLiteral("Backtrace: %1 frame(s), first Egoboard frame #%2\n")
                   .arg(data.frames.size())
                   .arg(data.firstEgoboardFrame);
        for (const Frame &frame : data.frames) {
            const QString marker = frame.index == data.firstEgoboardFrame ? QStringLiteral("=> ")
                                                                          : QStringLiteral("   ");
            QString location = frame.file;
            if (frame.line >= 0)
                location += QStringLiteral(":%1").arg(frame.line);
            if (location.isEmpty())
                location = frame.module;
            if (location.isEmpty())
                location = frame.address;
            out += QStringLiteral("%1#%2 %3%4\n")
                       .arg(marker)
                       .arg(frame.index)
                       .arg(frame.function)
                       .arg(location.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(location));
        }
    } else {
        out += QStringLiteral("Backtrace: %1\n").arg(
            data.backtraceNote.isEmpty() ? QStringLiteral("none attached") : data.backtraceNote);
        if (!data.backtraceRaw.isEmpty())
            out += QStringLiteral("Raw backtrace:\n%1\n").arg(data.backtraceRaw);
    }
    out += QStringLiteral("Logs (%1, %2 line(s)):\n").arg(data.logSource).arg(data.logs.size());
    for (const QString &line : data.logs)
        out += QStringLiteral("  %1\n").arg(line);
    if (!data.kwinInfo.isEmpty())
        out += QStringLiteral("KWin: %1\n").arg(data.kwinInfo);
    out += QStringLiteral("Tools:\n");
    for (const ToolInfo &tool : data.tools) {
        out += QStringLiteral("  %1: %2%3 — %4\n")
                   .arg(tool.name,
                        tool.available ? QStringLiteral("available") : QStringLiteral("missing"),
                        tool.version.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(tool.version),
                        tool.note);
    }
    out += QStringLiteral("Included: %1\n").arg(data.includedSections.join(QStringLiteral(", ")));
    out += QStringLiteral("Privacy: no clipboard entries, history.db, image blobs, keys or full "
                          "home paths are included; network upload is out of scope.\n");
    return out;
}

} // namespace CrashReport
