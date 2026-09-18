#pragma once

#include <KRunner/AbstractRunner>
#include <QDBusInterface>

#include "EntryRow.h"
#include "RunnerActions.h"

class EgoboardRunner : public KRunner::AbstractRunner {
    Q_OBJECT
public:
    EgoboardRunner(QObject *parent, const KPluginMetaData &metaData);

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;

    // Icon for a content type id from EntryRow ("text", "html", "image", "files").
    static QString iconForType(const QString &typeId);
    // Icon and label of a per-match action in the KRunner action menu.
    static QString actionIcon(RunnerActions::Kind kind);
    static QString actionLabel(RunnerActions::Kind kind, bool pinned);
    // "Firefox · text · pinned" — what KRunner shows under the preview.
    static QString subtextFor(const EntryRow &row);

private:
    static QDBusInterface *iface();
};
