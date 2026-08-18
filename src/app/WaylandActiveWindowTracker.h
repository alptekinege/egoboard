#pragma once

#include "ActiveWindowTracker.h"

#include <QObject>

// Active toplevel on Plasma/Wayland via the (vendored) wlr-foreign-toplevel
// management protocol, which KWin supports. Provides app_id + title of the
// activated toplevel; PID is not exposed by the protocol.
class WaylandActiveWindowTracker : public QObject, public IActiveWindowTracker {
    Q_OBJECT
public:
    explicit WaylandActiveWindowTracker(QObject *parent = nullptr);
    ~WaylandActiveWindowTracker() override;

    ActiveWindowInfo activeWindow() const override;

signals:
    void activeWindowChanged();

private:
    class Manager;
    Manager *m_manager = nullptr; // null when not on Wayland / unsupported
};
