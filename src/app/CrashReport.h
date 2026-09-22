#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// Local-first crash/support reports (U20). Everything here is QtCore-only so
// the schema, redaction and parsers stay unit-testable offscreen; both the
// CLI (`--crash-report` / `--read-crash-report`) and Settings ▸ Diagnostics
// drive this same code, which is what keeps them in parity.
//
// Privacy: collection never touches history.db, the clipboard, image blobs,
// KWallet/SQLCipher material or the full environment (a small allowlist of
// session variables is read instead). Paths are redacted to `~` before the
// bundle is shown or saved, and the caller previews the bundle first.
namespace CrashReport {

// Bounded windows: reports stay small no matter how chatty the journal is.
constexpr int kMaxLogLines = 200;
constexpr qint64 kMaxLogBytes = 65536;
constexpr int kMaxFrames = 64;
constexpr qint64 kMaxBacktraceBytes = 32768;
constexpr int kMaxToolOutputBytes = 16384;

struct Frame {
    int index = -1;
    QString function; // "??" when the address could not be symbolized
    QString file;
    int line = -1;
    QString address;
    QString module;
    bool isEgoboard = false;
};

struct ToolInfo {
    QString name;
    bool available = false;
    QString version; // first line of --version, empty when unknown
    QString note; // manual command / fallback when unavailable
};

struct Data {
    QString formatTag = QStringLiteral("egoboard-crash-report");
    int formatVersion = 1;
    QString appVersion;
    QString buildId; // GNU build ID hex, empty when unreadable
    bool hasDebugSymbols = false;
    QString symbolNote; // why a trace may be unsymbolized + how to fix
    QString qtVersion;
    QString kf6Version;
    QString qpa;
    QString sessionType; // XDG_SESSION_TYPE allowlist
    QString sessionDesktop; // XDG_SESSION_DESKTOP/XDG_CURRENT_DESKTOP allowlist
    QString kernel;
    QString architecture;
    QString executablePath; // redacted
    int signalNumber = 0; // 0 = live report, no crash captured
    QString signalName;
    QString crashingThread;
    QVector<Frame> frames;
    int firstEgoboardFrame = -1;
    QString backtraceNote;
    QString backtraceRaw; // capped source text, kept for debugging
    QStringList logs; // redacted, capped
    QString logSource; // where the logs came from, or the manual command
    QString kwinInfo;
    QVector<ToolInfo> tools;
    QStringList includedSections; // shown in the pre-save preview
    QString collectedAt;
};

// Inputs. Callers never pass history, clipboard or secret material.
struct Env {
    QString appVersion;
    QString executablePath; // empty = QCoreApplication::applicationFilePath()
    QString qpa; // empty = detected when a GUI app instance exists
    QString kf6Version; // empty = compile-time KF6 version or "unknown"
    int signalNumber = 0;
    QString crashingThread;
    QString gdbBacktraceRaw; // pre-captured `bt` output, or empty
    QStringList extraLogs; // recent in-app lines (redacted inside)
    bool runToolProbes = true; // QProcess probes (off in hermetic tests)
};

// Collects a report. Never throws, never returns secrets, never requires
// root; missing tools degrade to manual-command notes.
Data collect(const Env &env);

QJsonObject toJson(const Data &data);
// Tolerant reader: unknown keys are ignored (forward-compatible), missing
// sections default to empty. False only for a non-object or an explicit
// foreign format tag — never on partial data.
bool fromJson(const QJsonObject &object, Data *data, QString *error = nullptr);

// Human rendering shared by the CLI and Settings (highlights the first
// Egoboard frame, keeps raw tool output available at the end).
QString renderSummary(const Data &data);

// Path/username redaction for anything shown or saved.
QString redactForSharing(const QString &text);

// "SIGSEGV" for 11, "signal 99" for the unknown.
QString signalNameFor(int signalNumber);

// Parses `gdb bt` output (tolerant: garbage yields no frames, never crashes).
QVector<Frame> parseGdbBacktrace(const QString &text);
int firstEgoboardFrameIndex(const QVector<Frame> &frames);

// GNU build ID + .debug_info presence from the executable itself
// (bounds-checked ELF walk; false/empty on any anomaly, never crashes).
QString readBuildId(const QString &executablePath);
bool hasDebugSymbols(const QString &executablePath);

} // namespace CrashReport
