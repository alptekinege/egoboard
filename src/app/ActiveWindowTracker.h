#pragma once

#include <QString>

struct ActiveWindowInfo {
    QString appIdentifier; // process name (X11) or app_id (Wayland); may be empty
    QString windowTitle;

    bool isEmpty() const { return appIdentifier.isEmpty() && windowTitle.isEmpty(); }
};

// Seam for capturing "which app did this clipboard content come from".
// Implementations: X11ActiveWindowTracker (KWindowSystem/EWMH),
// WaylandActiveWindowTracker (wlr-foreign-toplevel-management).
class IActiveWindowTracker {
public:
    virtual ~IActiveWindowTracker() = default;
    virtual ActiveWindowInfo activeWindow() const = 0;
};
