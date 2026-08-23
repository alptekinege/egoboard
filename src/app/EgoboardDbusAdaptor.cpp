#include "EgoboardDbusAdaptor.h"

#include "IClipboardStorage.h"
#include "FilterSpec.h"

#include <QDBusConnection>
#include <QDBusMessage>

EgoboardDbusAdaptor::EgoboardDbusAdaptor(IClipboardStorage *storage, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
{
}

bool EgoboardDbusAdaptor::registerService()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(QStringLiteral("org.egoboard.Egoboard")))
        return false;
    bus.registerObject(QStringLiteral("/org/egoboard/Egoboard"), this,
                       QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    return true;
}

QStringList EgoboardDbusAdaptor::Search(const QString &query, int limit)
{
    QStringList out;
    if (!m_storage) return out;
    if (limit <= 0) limit = 10;
    limit = qBound(1, limit, 50);
    FilterSpec filter;
    filter.searchText = query;
    bool hasMore = false;
    const auto page = m_storage->fetchPage(filter, {}, limit, &hasMore);
    out.reserve(page.size());
    for (const auto &r : page) {
        // id<TAB>preview — easy to parse from qdbus / scripts
        out.append(QString::number(r.id) + QLatin1Char('\t') + r.preview.left(200).replace(QLatin1Char('\n'), QLatin1Char(' ')));
    }
    return out;
}
