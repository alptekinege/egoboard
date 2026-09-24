#pragma once

#include <QMap>
#include <QObject>
#include <QString>

class QTimer;

// Screencast awareness (R6, best-effort): reports whether the screen is being
// shared so the UI can indicate it and blur payload previews. Detection reads
// the local PipeWire registry for portal/compositor capture sources —
// cameras, microphones and ordinary app streams never count. Absence of
// evidence is not evidence: without the PipeWire build flag, without a
// daemon, or without a recognizable capture node the answer stays inactive
// (graceful, like the other optional subsystems).
class ScreencastWatcher : public QObject {
    Q_OBJECT
public:
    explicit ScreencastWatcher(QObject *parent = nullptr);

    bool sharingActive() const { return m_active; }

    // Node-property heuristic (pure): a portal/compositor screen-capture
    // source. props are PipeWire node properties (media.class, node.name,
    // application.name, ...).
    static bool nodeIndicatesSharing(const QMap<QString, QString> &props);

    // One synchronous registry sweep; false with no daemon, no nodes, or no
    // sharing node. Cheap when the daemon answers, bounded when it does not.
    bool probeNow() { return probeOnce(); }

    // Starts the slow poll (plus one immediate probe in the background).
    void start();

signals:
    void sharingChanged(bool active);

private:
    static bool probeOnce(); // worker entry: no instance state touched
    void applyProbe(bool active);
    void poll(); // runs probeOnce() off the GUI thread, applies the outcome

    QTimer *m_timer = nullptr;
    bool m_active = false;
    bool m_polling = false; // one sweep at a time; polls never pile up
};
