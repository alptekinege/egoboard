#include "app/ApplicationContext.h"
#include "app/SettingsManager.h"
#include "app/SingleInstanceGuard.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QStandardPaths>
#include <QTimer>

#include "Version.h"

int main(int argc, char *argv[])
{
    // The CLI modes are usually piped or captured (CI, the AppImage validator);
    // Qt routes logging to journald when stderr is not a TTY, which would hide
    // their reports. Force console logging for those runs only.
    for (int i = 1; i < argc; ++i) {
        const QLatin1String arg(argv[i]);
        if (arg == QLatin1String("--smoke") || arg.startsWith(QLatin1String("--bench"))) {
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
    parser.process(application);

    const bool smoke = parser.isSet(smokeOption);
    const bool bench = parser.isSet(benchOption);
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
    if (!smoke && !bench && !guard.tryLock()) {
        // Already running: bring the existing instance up and exit quietly.
        guard.sendShow();
        return 0;
    }

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
