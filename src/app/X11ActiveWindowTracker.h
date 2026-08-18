#pragma once

#include "ActiveWindowTracker.h"

// Active window on X11 via KWindowSystem (EWMH): _NET_ACTIVE_WINDOW,
// _NET_WM_NAME / visible name, _NET_WM_PID -> /proc/<pid>/comm, WM_CLASS.
class X11ActiveWindowTracker : public IActiveWindowTracker {
public:
    ActiveWindowInfo activeWindow() const override;
};
