#include "KWinCursorTracker.h"

#include "LayerShellHelper.h"

#include <QDBus>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTimer>

namespace {
// Reports the global cursor position back to egoboard. Runs inside KWin.
constexpr auto kCursorScript = R"(
var p = workspace.cursorPos;
callDBus("org.egoboard.Egoboard", "/org/egoboard/Egoboard",
         "org.egoboard.Egoboard", "ReportCursorPos", p.x, p.y);
)";
constexpr auto kPluginName = "egoboard-cursor";
} // namespace

KWinCursorTracker::KWinCursorTracker(QObject *parent)
    : QObject(parent)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this] {
        if (!m_callback)
            return;
        const auto callback = std::move(m_callback);
        m_callback = nullptr;
        callback(QCursor::pos()); // KWin did not answer in time — best effort
    });
}

KWinCursorTracker *KWinCursorTracker::self()
{
    static KWinCursorTracker *instance = new KWinCursorTracker(QGuiApplication::instance());
    return instance;
}

bool KWinCursorTracker::ensureScriptLoaded()
{
    if (m_kwinUnavailable)
        return false;
    if (m_scriptId >= 0)
        return true;

    m_scriptPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/egoboard-cursor.js");
    if (!QFileInfo::exists(m_scriptPath)) {
        QFile script(m_scriptPath);
        if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        script.write(kCursorScript);
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
    call.setArguments({m_scriptPath, QStringLiteral(kPluginName)});
    QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        m_kwinUnavailable = true; // not KWin, or the scripting API is locked down
        return false;
    }
    m_scriptId = reply.arguments().first().toInt();
    return m_scriptId > 0;
}

bool KWinCursorTracker::runScript()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/Scripting/Script%1").arg(m_scriptId),
        QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
    QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 1000);
    return reply.type() == QDBusMessage::ReplyMessage;
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
    if (!tracker->ensureScriptLoaded() || !tracker->runScript()) {
        callback(QCursor::pos());
        return;
    }

    tracker->m_callback = callback;
    tracker->m_timeoutTimer->start(timeoutMs);
}

void KWinCursorTracker::reportGlobalPos(int x, int y)
{
    KWinCursorTracker *tracker = self();
    if (!tracker->m_callback)
        return;
    const auto callback = std::move(tracker->m_callback);
    tracker->m_callback = nullptr;
    tracker->m_timeoutTimer->stop();
    callback(QPoint(x, y));
}
