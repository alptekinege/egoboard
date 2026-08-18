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
    parser.process(application);

    const bool smoke = parser.isSet(smokeOption);
    const QString databasePath = smoke
        ? QDir(QDir::tempPath()).filePath(QStringLiteral("egoboard-smoke/history.db"))
        : SettingsManager::defaultDatabasePath();

    SingleInstanceGuard guard(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/instance.lock"),
        QStringLiteral("egoboard-%1").arg(qgetenv("USER").constData()));
    if (!smoke && !guard.tryLock()) {
        // Already running: bring the existing instance up and exit quietly.
        guard.sendShow();
        return 0;
    }

    ApplicationContext context(databasePath, !smoke);
    if (smoke)
        return context.smokeTest();

    QObject::connect(&guard, &SingleInstanceGuard::showRequested, &context,
                     &ApplicationContext::toggleMainWindow);

    context.start();
    return application.exec();
}
