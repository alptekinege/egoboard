#include "ScreencastWatcher.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include <QtConcurrent>

#ifdef EGOBOARD_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>

#include <mutex>
#endif

namespace {
// Re-poll interval: screencast sessions last minutes, so seconds are plenty
// and a wedged daemon can never pile up work (one sweep at a time).
constexpr int kPollIntervalMs = 5000;
} // namespace

#ifdef EGOBOARD_HAVE_PIPEWIRE
namespace {
// One synchronous registry sweep: connect, collect Node globals, quit on the
// core sync round-trip. Everything here runs on a worker thread.
struct RegistryDump {
    pw_main_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    spa_hook registryListener{};
    int syncSeq = 0;
    bool syncDone = false;
    QVector<QMap<QString, QString>> nodes;
};

void onRegistryGlobal(void *data, uint32_t id, uint32_t permissions, const char *type,
                       uint32_t version, const spa_dict *props)
{
    Q_UNUSED(id);
    Q_UNUSED(permissions);
    Q_UNUSED(version);
    auto *dump = static_cast<RegistryDump *>(data);
    if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || !props)
        return;
    QMap<QString, QString> map;
    const spa_dict_item *item = nullptr;
    spa_dict_for_each(item, props)
    {
        if (item->key && item->value)
            map.insert(QString::fromUtf8(item->key), QString::fromUtf8(item->value));
    }
    dump->nodes.append(map);
}

void onRegistryGlobalRemove(void *data, uint32_t id)
{
    Q_UNUSED(data);
    Q_UNUSED(id);
}

void onCoreDone(void *data, uint32_t id, int seq)
{
    Q_UNUSED(id);
    auto *dump = static_cast<RegistryDump *>(data);
    if (seq == dump->syncSeq) {
        dump->syncDone = true;
        pw_main_loop_quit(dump->loop);
    }
}

QVector<QMap<QString, QString>> dumpVideoNodes()
{
    // pw_init is process-global; run once, never torn down (clients that
    // connect per probe must not pull the runtime out from under each other).
    static std::once_flag initOnce;
    std::call_once(initOnce, [] { pw_init(nullptr, nullptr); });

    // Event tables are value-initialized and filled by assignment, so the
    // many unused slots stay zeroed without -Wextra noise.
    pw_registry_events registryEvents{};
    registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
    registryEvents.global = onRegistryGlobal;
    registryEvents.global_remove = onRegistryGlobalRemove;
    pw_core_events coreEvents{};
    coreEvents.version = PW_VERSION_CORE_EVENTS;
    coreEvents.done = onCoreDone;

    QVector<QMap<QString, QString>> nodes;
    RegistryDump dump;
    dump.loop = pw_main_loop_new(nullptr);
    if (!dump.loop)
        return nodes;
    pw_loop *loop = pw_main_loop_get_loop(dump.loop);
    dump.context = pw_context_new(loop, nullptr, 0);
    if (!dump.context) {
        pw_main_loop_destroy(dump.loop);
        return nodes;
    }
    // connect() hands back the core directly (NULL when no daemon answers).
    dump.core = pw_context_connect(dump.context, nullptr, 0);
    if (!dump.core) {
        pw_context_destroy(dump.context);
        pw_main_loop_destroy(dump.loop);
        return nodes;
    }
    dump.registry = pw_core_get_registry(dump.core, PW_VERSION_REGISTRY, 0);
    if (!dump.registry) {
        pw_core_disconnect(dump.core);
        pw_context_destroy(dump.context);
        pw_main_loop_destroy(dump.loop);
        return nodes;
    }
    pw_registry_add_listener(dump.registry, &dump.registryListener, &registryEvents, &dump);
    spa_hook coreListener{};
    pw_core_add_listener(dump.core, &coreListener, &coreEvents, &dump);
    dump.syncSeq = pw_core_sync(dump.core, PW_ID_CORE, 0);
    pw_main_loop_run(dump.loop); // returns on the sync round-trip above
    if (dump.syncDone)
        nodes = dump.nodes;
    spa_hook_remove(&coreListener);
    spa_hook_remove(&dump.registryListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy *>(dump.registry));
    pw_core_disconnect(dump.core);
    pw_context_destroy(dump.context);
    pw_main_loop_destroy(dump.loop);
    return nodes;
}
} // namespace
#endif // EGOBOARD_HAVE_PIPEWIRE

ScreencastWatcher::ScreencastWatcher(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ScreencastWatcher::poll);
}

void ScreencastWatcher::start()
{
    m_timer->start();
    poll(); // immediate first sweep, then the slow poll
}

bool ScreencastWatcher::nodeIndicatesSharing(const QMap<QString, QString> &props)
{
    // Only video graphs can carry the screen; audio, MIDI and unknown nodes
    // (webcam included until proven otherwise below) never count.
    if (!props.value(QStringLiteral("media.class")).startsWith(QStringLiteral("Video")))
        return false;
    const QString name = props.value(QStringLiteral("node.name"));
    if (name.contains(QStringLiteral("screencast"), Qt::CaseInsensitive))
        return true;
    // The desktop portal brokers every Wayland screen capture; its own graph
    // nodes (never a camera or microphone) mean a capture is being served.
    return props.value(QStringLiteral("application.name"))
        .compare(QStringLiteral("xdg-desktop-portal"), Qt::CaseInsensitive)
        == 0;
}

bool ScreencastWatcher::probeOnce()
{
#ifdef EGOBOARD_HAVE_PIPEWIRE
    const QVector<QMap<QString, QString>> nodes = dumpVideoNodes();
    for (const auto &props : nodes) {
        if (nodeIndicatesSharing(props))
            return true;
    }
#endif
    return false;
}

void ScreencastWatcher::applyProbe(bool active)
{
    m_polling = false;
    if (active == m_active)
        return;
    m_active = active;
    emit sharingChanged(active);
}

void ScreencastWatcher::poll()
{
    if (m_polling)
        return; // the previous sweep is still running; skip this tick
    m_polling = true;
    // Lifetime-safe handoff (U19 shape): the worker never touches the
    // instance, and both delivery hops drop out when it is gone, so teardown
    // can never race a late probe.
    QPointer<ScreencastWatcher> guard(this);
    QtConcurrent::run([guard] {
        const bool active = ScreencastWatcher::probeOnce();
        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, active] {
                if (guard)
                    guard->applyProbe(active);
            },
            Qt::QueuedConnection);
    });
}
