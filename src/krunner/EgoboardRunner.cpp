#include "EgoboardRunner.h"

#include <KLocalizedString>
#include <KPluginFactory>
#include <KRunner/QueryMatch>
#include <KRunner/RunnerContext>

#ifdef EGOBOARD_HAVE_KSERVICE
#include <KIO/ApplicationLauncherJob>
#include <KService>
#endif

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QIcon>
#include <QProcess>
#include <QStandardPaths>

K_PLUGIN_CLASS_WITH_JSON(EgoboardRunner, "egoboardrunner.json")

EgoboardRunner::EgoboardRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
{
    addSyntax(KRunner::RunnerSyntax(QStringLiteral("eb :q:"), QStringLiteral("Search Egoboard clipboard history")));
    setTriggerWords({QStringLiteral("eb ")});
    setMinLetterCount(1);
}

void EgoboardRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query();
    if (!query.startsWith(QStringLiteral("eb "), Qt::CaseInsensitive))
        return;
    const QString needle = query.mid(3).trimmed();

    QDBusInterface iface(QStringLiteral("org.egoboard.Egoboard"),
                         QStringLiteral("/org/egoboard/Egoboard"),
                         QStringLiteral("org.egoboard.Egoboard"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        KRunner::QueryMatch m(this);
        m.setText(i18n("Start Egoboard"));
        m.setSubtext(i18n("Egoboard is not running"));
        m.setIconName(QStringLiteral("edit-paste"));
        m.setData(QStringLiteral("__launch__"));
        context.addMatch(m);
        return;
    }

    QDBusReply<QStringList> reply = iface.call(QStringLiteral("Search"), needle, 8);
    if (!reply.isValid())
        return;
    const QStringList rows = reply.value();
    for (const QString &row : rows) {
        const int tab = row.indexOf(QLatin1Char('\t'));
        const QString id = tab >= 0 ? row.left(tab) : row;
        const QString preview = tab >= 0 ? row.mid(tab + 1) : row;
        if (id.isEmpty() || preview.isEmpty())
            continue;
        KRunner::QueryMatch m(this);
        m.setText(preview.left(120));
        m.setSubtext(i18n("Egoboard #%1", id));
        m.setIconName(QStringLiteral("edit-paste"));
        m.setData(id);
        m.setRelevance(0.9);
        context.addMatch(m);
    }
    if (rows.isEmpty()) {
        KRunner::QueryMatch m(this);
        m.setText(needle.isEmpty() ? i18n("No clipboard history yet")
                                   : i18n("No matches for \"%1\"", needle));
        m.setIconName(QStringLiteral("edit-paste"));
        m.setData(QString());
        context.addMatch(m);
    }
}

void EgoboardRunner::run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match)
{
    Q_UNUSED(context)
    const QString data = match.data().toString();
    if (data == QLatin1String("__launch__")) {
#ifdef EGOBOARD_HAVE_KSERVICE
        // The desktop entry knows where the app lives; AppImage installs are
        // not necessarily on PATH (and their Exec line carries the mount path).
        if (const KService::Ptr service =
                KService::serviceByDesktopName(QStringLiteral("org.egoboard.Egoboard"))) {
            auto *job = new KIO::ApplicationLauncherJob(service, this);
            job->start();
            return;
        }
#endif
        const QString executable = QStandardPaths::findExecutable(QStringLiteral("egoboard"));
        if (!executable.isEmpty()) {
            QProcess::startDetached(executable, {});
            return;
        }
        QProcess::startDetached(QStringLiteral("egoboard"), {});
        return;
    }
    if (data.isEmpty())
        return;
    bool ok = false;
    const qint64 id = data.toLongLong(&ok);
    if (!ok)
        return;
    QDBusInterface iface(QStringLiteral("org.egoboard.Egoboard"),
                         QStringLiteral("/org/egoboard/Egoboard"),
                         QStringLiteral("org.egoboard.Egoboard"),
                         QDBusConnection::sessionBus());
    if (iface.isValid())
        iface.call(QStringLiteral("Paste"), id);
}

#include "EgoboardRunner.moc"
