#include "WaylandActiveWindowTracker.h"

#include <QGuiApplication>
#include <QTimer>
#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

// Binds zwlr_foreign_toplevel_manager_v1 through Qt's client-extension
// machinery (handles registry wait, versioning and the shared wl_display).
// All events arrive on the GUI thread.
class WaylandActiveWindowTracker::Manager
    : public QWaylandClientExtensionTemplate<Manager>
    , public QtWayland::zwlr_foreign_toplevel_manager_v1
{
    Q_OBJECT
public:
    Manager()
        : QWaylandClientExtensionTemplate<Manager>(3)
    {
    }
    ~Manager() override
    {
        if (isActive())
            stop();
        qDeleteAll(m_handles);
    }

    ActiveWindowInfo active() const { return m_active; }

signals:
    void activeChanged();

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(
        struct ::zwlr_foreign_toplevel_handle_v1 *toplevel) override
    {
        m_handles.append(new Handle(this, toplevel));
    }

public:
    // The compositor withdrew the global (session teardown, compositor
    // restart): every handle is dead, and keeping them would report a stale
    // active window forever. The Qt wrapper does not expose the protocol's
    // finished event, so this runs from the extension's activeChanged(false).
    void reset()
    {
        qDeleteAll(m_handles);
        m_handles.clear();
        if (!m_active.appIdentifier.isEmpty() || !m_active.windowTitle.isEmpty()) {
            m_active = ActiveWindowInfo{};
            emit activeChanged();
        }
    }

private:
    // One opened window. Not a QObject; owned by this manager.
    class Handle : public QtWayland::zwlr_foreign_toplevel_handle_v1
    {
    public:
        explicit Handle(Manager *owner, struct ::zwlr_foreign_toplevel_handle_v1 *handle)
            : QtWayland::zwlr_foreign_toplevel_handle_v1(handle)
            , m_owner(owner)
        {
        }
        ~Handle() override
        {
            if (isInitialized())
                destroy();
        }

        QString appId;
        QString title;
        bool activated = false;
        bool closed = false;

    protected:
        void zwlr_foreign_toplevel_handle_v1_title(const QString &t) override { title = t; }
        void zwlr_foreign_toplevel_handle_v1_app_id(const QString &a) override { appId = a; }
        void zwlr_foreign_toplevel_handle_v1_state(wl_array *state) override
        {
            activated = false;
            const auto *flags = static_cast<const uint32_t *>(state->data);
            const size_t count = state->size / sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                if (flags[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
                    activated = true;
            }
        }
        void zwlr_foreign_toplevel_handle_v1_done() override
        {
            m_owner->recomputeActive();
        }
        void zwlr_foreign_toplevel_handle_v1_closed() override { closed = true; }

    private:
        Manager *m_owner = nullptr;
    };

    void recomputeActive()
    {
        ActiveWindowInfo updated;
        bool hasClosed = false;
        for (Handle *handle : std::as_const(m_handles)) {
            if (handle->closed) {
                hasClosed = true;
                continue;
            }
            if (handle->activated) {
                updated.appIdentifier = handle->appId;
                updated.windowTitle = handle->title;
                break;
            }
        }
        if (updated.appIdentifier != m_active.appIdentifier
            || updated.windowTitle != m_active.windowTitle) {
            m_active = updated;
            emit activeChanged();
        }
        // Delete dead proxies outside of the event dispatch that closed them.
        if (hasClosed)
            QTimer::singleShot(0, this, [this] { pruneClosed(); });
    }

    void pruneClosed()
    {
        for (int i = m_handles.size() - 1; i >= 0; --i) {
            if (m_handles.at(i)->closed) {
                delete m_handles.at(i);
                m_handles.removeAt(i);
            }
        }
    }

    QList<Handle *> m_handles;
    ActiveWindowInfo m_active;
};

WaylandActiveWindowTracker::WaylandActiveWindowTracker(QObject *parent)
    : QObject(parent)
{
    if (QGuiApplication::platformName() == QLatin1String("wayland")) {
        m_manager = new Manager;
        m_manager->setParent(this);
        connect(m_manager, &Manager::activeChanged, this,
                &WaylandActiveWindowTracker::activeWindowChanged);
        connect(m_manager, &QWaylandClientExtension::activeChanged, this, [this] {
            if (!m_manager->isActive())
                m_manager->reset(); // global withdrawn: drop stale toplevels
        });
    }
}

WaylandActiveWindowTracker::~WaylandActiveWindowTracker()
{
    delete m_manager;
}

ActiveWindowInfo WaylandActiveWindowTracker::activeWindow() const
{
    return m_manager ? m_manager->active() : ActiveWindowInfo{};
}

#include "WaylandActiveWindowTracker.moc"
