#include "EgoboardRunner.h"

#include <KPluginFactory>
#include <KRunner/QueryMatch>
#include <KRunner/RunnerContext>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QIcon>
#include <QProcess>

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
        m.setText(QStringLiteral("Start Egoboard"));
        m.setSubtext(QStringLiteral("Egoboard is not running"));
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
        m.setSubtext(QStringLiteral("Egoboard #%1").arg(id));
        m.setIconName(QStringLiteral("edit-paste"));
        m.setData(id);
        m.setRelevance(0.9);
        context.addMatch(m);
    }
    if (rows.isEmpty()) {
        KRunner::QueryMatch m(this);
        m.setText(needle.isEmpty() ? QStringLiteral("No clipboard history yet") : QStringLiteral("No matches for \"%1\"").arg(needle));
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
