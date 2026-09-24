#include "PortalPaster.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kRemoteDesktop = "org.freedesktop.portal.RemoteDesktop";
constexpr auto kRequestIface = "org.freedesktop.portal.Request";
constexpr auto kSessionIface = "org.freedesktop.portal.Session";
// SelectDevices types bitfield: keyboard only (no pointer capture).
constexpr uint kDeviceKeyboard = 1;
// Remember the grant until explicitly revoked, so consent is per session,
// not per paste; turning the toggle off stops all portal use.
constexpr uint kPersistUntilRevoked = 2;
// XKB keysyms for the Ctrl+V chord.
constexpr quint32 kKeysymControlL = 0xFFE3;
constexpr quint32 kKeysymV = 0x0076;
// One attempt (consent dialog included) never outlives this; expiry falls
// back to the notification path instead of hanging a paste.
constexpr int kSessionTimeoutMs = 30000;
} // namespace

PortalPaster::PortalPaster(QObject *parent)
    : QObject(parent)
{
    m_guard.setSingleShot(true);
    connect(&m_guard, &QTimer::timeout, this, &PortalPaster::failAttempt);
}

QString PortalPaster::portalServiceName()
{
    return QStringLiteral("org.freedesktop.portal.Desktop");
}

bool PortalPaster::isServicePresent(const QString &service)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    return bus.interface()->isServiceRegistered(service);
}

QVector<PortalPaster::KeyPress> PortalPaster::ctrlVPlan()
{
    return {
        {kKeysymControlL, true},
        {kKeysymV, true},
        {kKeysymV, false},
        {kKeysymControlL, false},
    };
}

PortalPaster::Method PortalPaster::pasteMethod(bool isWayland, bool portalReady)
{
    if (!isWayland)
        return Method::XTest; // xdotool/notification fallbacks stay in AutoPaster
    return portalReady ? Method::Portal : Method::Notification;
}

void PortalPaster::ensureSession()
{
    if (hasSession() || m_step != Step::Idle)
        return; // live, or an attempt already in flight
    if (!isAvailable()) {
        emit sessionChanged(false);
        return;
    }
    delete m_portal;
    m_portal = new QDBusInterface(portalServiceName(), QString::fromLatin1(kPortalPath),
                                  QString::fromLatin1(kRemoteDesktop),
                                  QDBusConnection::sessionBus(), this);
    m_step = Step::Creating;
    m_guard.start(kSessionTimeoutMs);
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"),
                   QStringLiteral("egoboard-%1").arg(++m_tokenSerial));
    options.insert(QStringLiteral("session_handle_token"),
                   QStringLiteral("egoboard-%1").arg(++m_tokenSerial));
    callRemoteDesktop(QStringLiteral("CreateSession"), {options});
}

void PortalPaster::closeSession()
{
    m_guard.stop();
    m_step = Step::Idle;
    if (!m_requestPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            portalServiceName(), m_requestPath, QString::fromLatin1(kRequestIface),
            QStringLiteral("Response"), this, SLOT(onPortalResponse(uint, QVariantMap)));
        m_requestPath.clear();
    }
    if (!m_sessionPath.isEmpty()) {
        QDBusInterface session(portalServiceName(), m_sessionPath,
                               QString::fromLatin1(kSessionIface),
                               QDBusConnection::sessionBus());
        session.asyncCall(QStringLiteral("Close"));
        m_sessionPath.clear();
        emit sessionChanged(false);
    }
    delete m_portal;
    m_portal = nullptr;
}

bool PortalPaster::paste()
{
    if (!hasSession() || !m_portal)
        return false;
    for (const KeyPress &press : ctrlVPlan()) {
        QVariantMap options;
        options.insert(QStringLiteral("handle_token"),
                       QStringLiteral("egoboard-%1").arg(++m_tokenSerial));
        // Fire-and-forget through the live session only: no reply to wait on,
        // so a dead session degrades to a no-op instead of a second paste.
        m_portal->asyncCall(QStringLiteral("NotifyKeyboardKeysym"),
                            QVariant::fromValue(QDBusObjectPath(m_sessionPath)), options,
                            press.keysym, press.press);
    }
    return true;
}

void PortalPaster::failAttempt()
{
    m_guard.stop();
    m_step = Step::Idle;
    if (!m_requestPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            portalServiceName(), m_requestPath, QString::fromLatin1(kRequestIface),
            QStringLiteral("Response"), this, SLOT(onPortalResponse(uint, QVariantMap)));
        m_requestPath.clear();
    }
    if (!m_sessionPath.isEmpty())
        m_sessionPath.clear();
    delete m_portal;
    m_portal = nullptr;
    emit sessionChanged(false);
}

void PortalPaster::callRemoteDesktop(const QString &method, const QVariantList &args)
{
    if (!m_portal) {
        failAttempt();
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
        portalServiceName(), QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kRemoteDesktop), method);
    call.setArguments(args);
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher](QDBusPendingCall *call) {
                watcher->deleteLater();
                const QDBusPendingReply<QDBusObjectPath> reply = *call;
                if (reply.isError() || !reply.isValid()) {
                    failAttempt();
                    return;
                }
                // The method result is the request path; the outcome arrives
                // later through the Response signal on that path.
                m_requestPath = reply.value().path();
                QDBusConnection::sessionBus().connect(
                    portalServiceName(), m_requestPath, QString::fromLatin1(kRequestIface),
                    QStringLiteral("Response"), this,
                    SLOT(onPortalResponse(uint, QVariantMap)));
            });
}

void PortalPaster::onPortalResponse(uint response, const QVariantMap &results)
{
    if (m_step == Step::Idle)
        return;
    QDBusConnection::sessionBus().disconnect(
        portalServiceName(), m_requestPath, QString::fromLatin1(kRequestIface),
        QStringLiteral("Response"), this, SLOT(onPortalResponse(uint, QVariantMap)));
    m_requestPath.clear();
    if (response != 0) { // denied or dismissed: fall back, never retry alone
        failAttempt();
        return;
    }
    if (m_step == Step::Creating) {
        const QString session = results.value(QStringLiteral("session_handle")).toString();
        if (session.isEmpty()) {
            failAttempt();
            return;
        }
        m_sessionPath = session;
        m_step = Step::Selecting;
        QVariantMap options;
        options.insert(QStringLiteral("handle_token"),
                       QStringLiteral("egoboard-%1").arg(++m_tokenSerial));
        options.insert(QStringLiteral("types"), kDeviceKeyboard);
        callRemoteDesktop(QStringLiteral("SelectDevices"),
                          {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), options});
        return;
    }
    if (m_step == Step::Selecting) {
        m_step = Step::Starting;
        QVariantMap options;
        options.insert(QStringLiteral("handle_token"),
                       QStringLiteral("egoboard-%1").arg(++m_tokenSerial));
        options.insert(QStringLiteral("persist_mode"), kPersistUntilRevoked);
        callRemoteDesktop(QStringLiteral("Start"),
                          {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), options});
        return;
    }
    // Starting answered granted (response == 0 checked above): the session
    // is live; further pastes only send keysyms, never another prompt.
    m_guard.stop();
    m_step = Step::Idle;
    emit sessionChanged(true);
}
