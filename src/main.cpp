#include "app/ApplicationContext.h"
#include "app/CrashReport.h"
#include "app/SettingsManager.h"
#include "app/SingleInstanceGuard.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include "Version.h"

#include "Version.h"

// Local support workflow (U20): `--crash-report [dir]` collects the same
// bounded, redacted bundle Settings ▸ Diagnostics previews, and
// `--read-crash-report <file>` renders it back. Both stay clear of the
// running instance and the history database.
int runCrashReportMode(const QString &targetDir, const QString &reportFile, bool write)
{
    QTextStream out(stdout);
    if (!write) {
        QFile file(reportFile);
        if (reportFile.trimmed().isEmpty() || !file.open(QIODevice::ReadOnly)) {
            QTextStream(stderr) << QStringLiteral("Cannot read %1.\n").arg(reportFile);
            return 1;
        }
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            QTextStream(stderr)
                << QStringLiteral("%1 is not a valid report file.\n").arg(reportFile);
            return 1;
        }
        CrashReport::Data data;
        QString error;
        if (!CrashReport::fromJson(document.object(), &data, &error)) {
            QTextStream(stderr) << error + QLatin1Char('\n');
            return 1;
        }
        out << CrashReport::renderSummary(data);
        return 0;
    }

    CrashReport::Env env;
    env.appVersion = QStringLiteral(EGOBOARD_VERSION);
    env.qpa = QGuiApplication::platformName();
    const CrashReport::Data data = CrashReport::collect(env);
    QDir dir(targetDir.trimmed().isEmpty() ? QDir::tempPath() : targetDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        QTextStream(stderr)
            << QStringLiteral("Cannot create the folder %1.\n").arg(dir.absolutePath());
        return 1;
    }
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString path =
        dir.filePath(QStringLiteral("egoboard-report-%1.json").arg(stamp));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream(stderr)
            << QStringLiteral("Cannot write %1: %2.\n").arg(path, file.errorString());
        return 1;
    }
    const QByteArray payload = QJsonDocument(CrashReport::toJson(data)).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        QTextStream(stderr)
            << QStringLiteral("Write to %1 failed: %2.\n").arg(path, file.errorString());
        return 1;
    }
    out << QStringLiteral("Report written to %1.\n").arg(path);
    out << CrashReport::renderSummary(data);
    return 0;
}

int main(int argc, char *argv[])
{
    // The CLI modes are usually piped or captured (CI, the AppImage validator);
    // Qt routes logging to journald when stderr is not a TTY, which would hide
    // their reports. Force console logging for those runs only.
    for (int i = 1; i < argc; ++i) {
        const QLatin1String arg(argv[i]);
        if (arg == QLatin1String("--smoke") || arg.startsWith(QLatin1String("--bench"))
            || arg == QLatin1String("--crash-report") || arg.startsWith(QLatin1String("--crash-report="))
            || arg == QLatin1String("--read-crash-report")
            || arg.startsWith(QLatin1String("--read-crash-report="))) {
            qputenv("QT_FORCE_STDERR_LOGGING", "1");
            break;
        }
    }

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("egoboard"));
    QApplication::setApplicationVersion(QStringLiteral(EGOBOARD_VERSION));
    QApplication::setWindowIcon(QIcon::fromTheme(
        QStringLiteral("egoboard"), QIcon(QStringLiteral(":/icons/egoboard.svg"))));
    QApplication::setQuitOnLastWindowClosed(false); // lives in the tray

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Clipboard history manager for KDE Plasma (X11 and Wayland)."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption smokeOption(QStringLiteral("smoke"),
                                   QStringLiteral("Run a headless self-check and exit."));
    parser.addOption(smokeOption);
    QCommandLineOption benchOption(
        QStringLiteral("bench"),
        QStringLiteral("Run a synthetic scale benchmark and exit (optional entry count)."),
        QStringLiteral("entries"), QStringLiteral("50000"));
    parser.addOption(benchOption);
    QCommandLineOption crashReportOption(
        QStringLiteral("crash-report"),
        QStringLiteral("Write a local crash/support report and exit (optional target folder)."),
        QStringLiteral("dir"), QString());
    parser.addOption(crashReportOption);
    QCommandLineOption readReportOption(
        QStringLiteral("read-crash-report"),
        QStringLiteral("Render a crash report file and exit."),
        QStringLiteral("file"));
    parser.addOption(readReportOption);
    parser.process(application);

    const bool smoke = parser.isSet(smokeOption);
    const bool bench = parser.isSet(benchOption);
    const bool crashReport = parser.isSet(crashReportOption);
    const bool readReport = parser.isSet(readReportOption);
    QString databasePath = SettingsManager::defaultDatabasePath();
    if (smoke) {
        // Start from a clean scratch database so repeated runs are idempotent.
        const QString smokeDir = QDir(QDir::tempPath()).filePath(QStringLiteral("egoboard-smoke"));
        QDir(smokeDir).removeRecursively();
        databasePath = QDir(smokeDir).filePath(QStringLiteral("history.db"));
    } else if (bench) {
        // Start from a clean scratch database so the timings are comparable.
        const QString benchDir = QDir(QDir::tempPath()).filePath(QStringLiteral("egoboard-bench"));
        QDir(benchDir).removeRecursively();
        databasePath = QDir(benchDir).filePath(QStringLiteral("history.db"));
    }

    SingleInstanceGuard guard(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/instance.lock"),
        QStringLiteral("egoboard-%1").arg(qgetenv("USER").constData()));
    if (!smoke && !bench && !crashReport && !readReport && !guard.tryLock()) {
        // Already running: bring the existing instance up and exit quietly.
        guard.sendShow();
        return 0;
    }

    // Support modes leave the running instance (and its database) alone.
    if (crashReport || readReport)
        return runCrashReportMode(parser.value(crashReportOption), parser.value(readReportOption),
                                  crashReport);

    ApplicationContext context(databasePath, !smoke && !bench);
    if (smoke)
        return context.smokeTest();
    if (bench)
        return context.benchmark(parser.value(benchOption).toInt());

    QObject::connect(&guard, &SingleInstanceGuard::showRequested, &context,
                     &ApplicationContext::toggleMainWindow);

    context.start();
    return application.exec();
}
