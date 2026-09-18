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
        out.append(QString::number(r.id) + QLatin1Char('\t') + r.preview.left(200).replace(QLatin1Char('\n'), QLatin1Char(' ')));
    }
    return out;
}

QStringList EgoboardDbusAdaptor::SearchDetailed(const QString &query, int limit)
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
    for (const ClipboardRecord &record : page) {
        EntryRow row;
        row.id = record.id;
        row.type = EntryRow::contentTypeId(record.type);
        row.sourceApp = record.sourceApp;
        row.sourceWindow = record.sourceWindow;
        row.pinned = record.pinned;
        row.timestamp = record.timestamp;
        row.preview = record.preview.isEmpty() ? record.textData : record.preview;
        out.append(row.encode());
    }
    return out;
}

QString EgoboardDbusAdaptor::Preview(qint64 id)
{
    if (!m_storage)
        return {};
    ClipboardRecord record;
    if (!m_storage->fetchFull(id, &record))
        return {};
    const QString text = record.textData.isEmpty() ? record.preview : record.textData;
    return text.left(EntryRow::kMaxPreviewChars);
}

bool EgoboardDbusAdaptor::entryExists(qint64 id) const
{
    if (!m_storage || id <= 0)
        return false;
    ClipboardRecord record;
    return m_storage->fetchFull(id, &record);
}

bool EgoboardDbusAdaptor::Paste(qint64 id)
{
    emit pasteRequested(id);
    return true;
}

bool EgoboardDbusAdaptor::Copy(qint64 id)
{
    if (!entryExists(id))
        return false;
    emit copyRequested(id);
    return true;
}

bool EgoboardDbusAdaptor::Pin(qint64 id, bool pinned)
{
    if (!entryExists(id))
        return false;
    emit pinRequested(id, pinned);
    return true;
}

bool EgoboardDbusAdaptor::Delete(qint64 id)
{
    if (!entryExists(id))
        return false;
    emit deleteRequested(id);
    return true;
}

bool EgoboardDbusAdaptor::ShowQuickPaste()
{
    emit showQuickPasteRequested();
    return true;
}

void EgoboardDbusAdaptor::ReportCursorPos(int x, int y)
{
    emit cursorPosReported(x, y);
}
