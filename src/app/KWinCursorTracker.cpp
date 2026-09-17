#include "KWinCursorTracker.h"

#include "LayerShellHelper.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QGuiApplication>
#include <QTemporaryFile>
#include <QTimer>

namespace {
// Reports the global cursor position back to egoboard. Runs inside KWin.
// Math.round keeps the arguments integral — KWin marshals integral JS numbers
// as D-Bus int32 (fractional ones would arrive as double and miss the slot).
constexpr auto kCursorScript = R"(
var p = workspace.cursorPos;
callDBus("org.egoboard.Egoboard", "/org/egoboard/Egoboard",
         "org.egoboard.Egoboard", "ReportCursorPos", Math.round(p.x), Math.round(p.y));
)";
} // namespace

KWinCursorTracker::KWinCursorTracker(QObject *parent)
    : QObject(parent)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this] {
        if (!m_callback)
            return;
        const QString pluginName = m_activeScriptName;
        const auto callback = std::move(m_callback);
        m_callback = nullptr;
        unloadScript(pluginName);
        callback(QCursor::pos()); // KWin did not answer in time — best effort
    });
}

KWinCursorTracker *KWinCursorTracker::self()
{
    static KWinCursorTracker *instance = new KWinCursorTracker(QGuiApplication::instance());
    return instance;
}

bool KWinCursorTracker::loadAndRunScript(const QString &filePath, const QString &pluginName)
{
    // Reusing a plugin name collides with KWin's still-pending unload of the
    // previous one, so every query gets a fresh name.
    QDBusMessage load = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
    load.setArguments({filePath, pluginName});
    QDBusMessage loadReply = QDBusConnection::sessionBus().call(load, QDBus::Block, 1000);
    if (loadReply.type() != QDBusMessage::ReplyMessage || loadReply.arguments().isEmpty())
        return false;
    const int scriptId = loadReply.arguments().first().toInt();
    if (scriptId <= 0)
        return false;

    QDBusMessage run = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(scriptId),
        QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
    QDBusMessage runReply = QDBusConnection::sessionBus().call(run, QDBus::Block, 1000);
    return runReply.type() == QDBusMessage::ReplyMessage;
}

void KWinCursorTracker::unloadScript(const QString &pluginName)
{
    // Fire-and-forget: KWin drops the script from its session.
    QDBusMessage unload = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
    unload.setArguments({pluginName});
    QDBusConnection::sessionBus().call(unload, QDBus::NoBlock);
}

void KWinCursorTracker::queryGlobal(const std::function<void(const QPoint &)> &callback,
                                    int timeoutMs)
{
    KWinCursorTracker *tracker = self();

    // X11 reports the exact position; off KWin-Wayland this is best effort.
    if (!LayerShellHelper::isWayland() || tracker->m_kwinUnavailable) {
        callback(QCursor::pos());
        return;
    }

    // Unique, owner-only temp file: a fixed /tmp name is world-writable and
    // KWin only needs it until the run call returns; Qt removes it at scope
    // exit. The plugin name is unique per query because KWin's unload of a
    // previous script is asynchronous (a reused name can run a stale script).
    QTemporaryFile script(QDir::tempPath() + QStringLiteral("/egoboard-cursor-XXXXXX.js"));
    if (!script.open()) {
        tracker->m_kwinUnavailable = true;
        callback(QCursor::pos());
        return;
    }
    if (script.write(kCursorScript) != qstrlen(kCursorScript) || !script.flush()) {
        tracker->m_kwinUnavailable = true;
        callback(QCursor::pos());
        return;
    }

    const QString pluginBase =
        QStringLiteral("egoboard-cursor-%1-%2")
            .arg(QCoreApplication::applicationPid())
            .arg(++tracker->m_loadCounter);

    bool started = false;
    QString loadedName;
    for (int attempt = 0; attempt < 3 && !started; ++attempt) {
        const QString candidate = attempt > 0
            ? pluginBase + QStringLiteral("-%1").arg(attempt)
            : pluginBase;
        started = tracker->loadAndRunScript(script.fileName(), candidate);
        if (started)
            loadedName = candidate;
    }
    if (!started) {
        tracker->m_kwinUnavailable = true; // not KWin, or the scripting API is locked down
        callback(QCursor::pos());
        return;
    }

    // Unload must use the exact name that was loaded (retries add a suffix).
    tracker->m_activeScriptName = loadedName;
    tracker->m_callback = callback;
    tracker->m_timeoutTimer->start(timeoutMs);
}

void KWinCursorTracker::reportGlobalPos(int x, int y)
{
    KWinCursorTracker *tracker = self();
    if (!tracker->m_callback)
        return;
    const QString pluginName = tracker->m_activeScriptName;
    const auto callback = std::move(tracker->m_callback);
    tracker->m_callback = nullptr;
    tracker->m_timeoutTimer->stop();
    tracker->unloadScript(pluginName);
    callback(QPoint(x, y));
}
