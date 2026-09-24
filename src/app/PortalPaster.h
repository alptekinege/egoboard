#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class QDBusInterface;

// Wayland portal paste (opt-in): asks the compositor to press Ctrl+V through
// org.freedesktop.portal RemoteDesktop after its own permission prompt, so
// pasting works where client key injection is prohibited. Best-effort by
// design: a missing portal, denied consent or timeout simply leaves the
// session down and the caller keeps the manual-paste notification fallback —
// pastes are only ever sent through a live session, never twice.
class PortalPaster : public QObject {
    Q_OBJECT
public:
    explicit PortalPaster(QObject *parent = nullptr);

    static QString portalServiceName(); // "org.freedesktop.portal.Desktop"
    // Sync probe for a service on the session bus (cheap; false with no bus).
    static bool isServicePresent(const QString &service);
    static bool isAvailable() { return isServicePresent(portalServiceName()); }

    // Ctrl+V as portal keysym presses (XKB keysyms with press states).
    struct KeyPress {
        quint32 keysym;
        bool press;
    };
    static QVector<KeyPress> ctrlVPlan();

    // First-choice paste method (pure): X11 always tries key injection first
    // (the xdotool/notification fallbacks stay inside AutoPaster); Wayland
    // uses the portal only when opted in AND its session is live.
    enum class Method { XTest, Portal, Notification };
    static Method pasteMethod(bool isWayland, bool portalReady);

    // Begin the per-session consent flow (async; the compositor may prompt).
    // Reports through sessionChanged(): true once the session is live.
    void ensureSession();
    void closeSession();
    bool hasSession() const { return !m_sessionPath.isEmpty(); }

    // Sends Ctrl+V through the live session; false without one (the caller
    // falls back to the notification path — never hangs, never double-pastes).
    bool paste();

signals:
    void sessionChanged(bool ready);

private:
    void failAttempt();
    void onPortalResponse(uint response, const QVariantMap &results);
    void callRemoteDesktop(const QString &method, const QVariantList &args);

    enum class Step { Idle, Creating, Selecting, Starting };
    Step m_step = Step::Idle;
    QString m_sessionPath;
    QString m_requestPath;
    QTimer m_guard;
    QDBusInterface *m_portal = nullptr; // RemoteDesktop endpoint, owned here
    int m_tokenSerial = 0;
};
