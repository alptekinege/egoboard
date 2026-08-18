#include "X11ActiveWindowTracker.h"

#include <KWindowInfo>
#include <KX11Extras>

#include <QFile>
#include <QGuiApplication>
#include <QtGlobal>

namespace {

QString processNameFromPid(qint64 pid)
{
    if (pid <= 0)
        return {};
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly))
        return {};
    const QString name = QString::fromUtf8(comm.readAll().trimmed());
    return name;
}

} // namespace

ActiveWindowInfo X11ActiveWindowTracker::activeWindow() const
{
    ActiveWindowInfo info;
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return info;

    const KWindowInfo window(KX11Extras::activeWindow(),
                             NET::WMVisibleName | NET::WMName | NET::WMPid,
                             NET::WM2WindowClass);
    if (!window.valid())
        return info;

    info.windowTitle = window.visibleName().isEmpty() ? window.name() : window.visibleName();
    info.appIdentifier = processNameFromPid(window.pid());
    if (info.appIdentifier.isEmpty())
        info.appIdentifier = window.windowClassClass();
    return info;
}
