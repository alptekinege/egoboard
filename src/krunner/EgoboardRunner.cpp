#include "EgoboardRunner.h"

#include <KLocalizedString>
#include <KPluginFactory>
#include <KRunner/Action>
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

#include <memory>

K_PLUGIN_CLASS_WITH_JSON(EgoboardRunner, "egoboardrunner.json")

namespace {
constexpr int kMaxMatches = 8;
constexpr int kServerPreviewChars = 200; // legacy Search() rows
}

EgoboardRunner::EgoboardRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
{
    addSyntax(KRunner::RunnerSyntax(QStringLiteral("eb :q:"), QStringLiteral("Search Egoboard clipboard history")));
    setTriggerWords({QStringLiteral("eb ")});
    setMinLetterCount(1);
}

QDBusInterface *EgoboardRunner::iface()
{
    // Freed by the caller's std::unique_ptr; a fresh proxy per call keeps the
    // runner usable when Egoboard is (re)started while KRunner stays alive.
    return new QDBusInterface(QStringLiteral("org.egoboard.Egoboard"),
                              QStringLiteral("/org/egoboard/Egoboard"),
                              QStringLiteral("org.egoboard.Egoboard"),
                              QDBusConnection::sessionBus());
}

QString EgoboardRunner::iconForType(const QString &typeId)
{
    if (typeId == QLatin1String("image"))
        return QStringLiteral("image-x-generic");
    if (typeId == QLatin1String("files"))
        return QStringLiteral("folder");
    if (typeId == QLatin1String("html"))
        return QStringLiteral("text-html");
    return QStringLiteral("edit-paste");
}

QString EgoboardRunner::actionIcon(RunnerActions::Kind kind)
{
    switch (kind) {
    case RunnerActions::Kind::Copy:
        return QStringLiteral("edit-copy");
    case RunnerActions::Kind::Pin:
    case RunnerActions::Kind::Unpin:
        return QStringLiteral("bookmark-new");
    case RunnerActions::Kind::Delete:
        return QStringLiteral("edit-delete");
    case RunnerActions::Kind::Paste:
        break;
    }
    return QStringLiteral("edit-paste");
}

QString EgoboardRunner::actionLabel(RunnerActions::Kind kind, bool pinned)
{
    switch (kind) {
    case RunnerActions::Kind::Copy:
        return i18n("Copy");
    case RunnerActions::Kind::Pin:
        return i18n("Pin");
    case RunnerActions::Kind::Unpin:
        return i18n("Unpin");
    case RunnerActions::Kind::Delete:
        return i18n("Delete");
    case RunnerActions::Kind::Paste:
        break;
    }
    Q_UNUSED(pinned)
    return i18n("Paste");
}

QString EgoboardRunner::subtextFor(const EntryRow &row)
{
    QStringList parts;
    const QString summary = row.summary();
    if (!summary.isEmpty())
        parts << summary;
    if (row.pinned)
        parts << i18n("pinned");
    parts << i18n("Egoboard #%1", row.id);
    return parts.join(QStringLiteral(" · "));
}

void EgoboardRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query();
    if (!query.startsWith(QStringLiteral("eb "), Qt::CaseInsensitive))
        return;
    const QString needle = query.mid(3).trimmed();

    std::unique_ptr<QDBusInterface> proxy(iface());
    if (!proxy->isValid()) {
        KRunner::QueryMatch m(this);
        m.setText(i18n("Start Egoboard"));
        m.setSubtext(i18n("Egoboard is not running"));
        m.setIconName(QStringLiteral("edit-paste"));
        m.setData(QStringLiteral("__launch__"));
        context.addMatch(m);
        return;
    }

    // Detailed rows carry type/app/pinned, but an older running instance only
    // knows Search(); fall back so the runner keeps working after an upgrade
    // that has not restarted the application yet.
    QStringList rows;
    const QDBusReply<QStringList> detailedReply =
        proxy->call(QStringLiteral("SearchDetailed"), needle, kMaxMatches);
    if (detailedReply.isValid()) {
        rows = detailedReply.value();
    } else {
        const QDBusReply<QStringList> legacyReply =
            proxy->call(QStringLiteral("Search"), needle, kMaxMatches);
        if (!legacyReply.isValid())
            return;
        for (const QString &row : legacyReply.value()) {
            const int tab = row.indexOf(QLatin1Char('\t'));
            if (tab < 0)
                continue;
            EntryRow entry; // id + preview only; the rest stays unknown
            entry.id = row.left(tab).toLongLong();
            entry.preview = row.mid(tab + 1).left(kServerPreviewChars);
            if (entry.isValid())
                rows.append(entry.encode());
        }
    }

    int shown = 0;
    for (const QString &row : rows) {
        const EntryRow entry = EntryRow::decode(row);
        if (!entry.isValid() || entry.preview.isEmpty())
            continue;
        ++shown;

        KRunner::QueryMatch m(this);
        m.setText(entry.preview);
        m.setMultiLine(true);
        m.setSubtext(subtextFor(entry));
        m.setIconName(iconForType(entry.type));
        m.setData(QString::number(entry.id));
        // Pinned entries are what the user deliberately kept: rank them up.
        m.setRelevance(entry.pinned ? 0.95 : 0.9);
        // Pin and Unpin are one kind in the menu, chosen by the entry's state.
        const RunnerActions::Kind pinKind =
            entry.pinned ? RunnerActions::Kind::Unpin : RunnerActions::Kind::Pin;
        const QVector<RunnerActions::Kind> kinds = {RunnerActions::Kind::Paste,
                                                    RunnerActions::Kind::Copy, pinKind,
                                                    RunnerActions::Kind::Delete};
        QList<KRunner::Action> actions;
        actions.reserve(kinds.size());
        for (RunnerActions::Kind kind : kinds) {
            actions.append(KRunner::Action(RunnerActions::id(kind), actionIcon(kind),
                                           actionLabel(kind, entry.pinned)));
        }
        m.setActions(actions);
        context.addMatch(m);
    }
    if (shown == 0) {
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

    std::unique_ptr<QDBusInterface> proxy(iface());
    if (!proxy->isValid())
        return;
    // The selected action (KRunner passes its id back); the plain Enter match
    // pastes, which is also the fallback for an id we do not know.
    const RunnerActions::Kind kind = RunnerActions::kindForId(match.selectedAction().id());
    const QString method = RunnerActions::dbusMethod(kind);
    if (method == QLatin1String("Pin"))
        proxy->call(method, id, RunnerActions::pinnedFlag(kind));
    else
        proxy->call(method, id);
}

#include "EgoboardRunner.moc"
