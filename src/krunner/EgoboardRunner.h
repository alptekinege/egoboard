#pragma once

#include <KRunner/AbstractRunner>
#include <QDBusInterface>

class EgoboardRunner : public KRunner::AbstractRunner {
    Q_OBJECT
public:
    EgoboardRunner(QObject *parent, const KPluginMetaData &metaData);

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;
};
