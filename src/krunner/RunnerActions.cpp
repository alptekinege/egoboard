#include "RunnerActions.h"

namespace RunnerActions {

QString id(Kind kind)
{
    switch (kind) {
    case Kind::Paste:
        return QStringLiteral("egoboard-paste");
    case Kind::Copy:
        return QStringLiteral("egoboard-copy");
    case Kind::Pin:
        return QStringLiteral("egoboard-pin");
    case Kind::Unpin:
        return QStringLiteral("egoboard-unpin");
    case Kind::Delete:
        return QStringLiteral("egoboard-delete");
    }
    return QStringLiteral("egoboard-paste");
}

QString dbusMethod(Kind kind)
{
    switch (kind) {
    case Kind::Copy:
        return QStringLiteral("Copy");
    case Kind::Pin:
    case Kind::Unpin:
        return QStringLiteral("Pin");
    case Kind::Delete:
        return QStringLiteral("Delete");
    case Kind::Paste:
        break;
    }
    return QStringLiteral("Paste");
}

bool pinnedFlag(Kind kind)
{
    return kind == Kind::Pin;
}

Kind kindForId(const QString &actionId)
{
    for (Kind kind : all()) {
        if (id(kind) == actionId)
            return kind;
    }
    return Kind::Paste;
}

QVector<Kind> all()
{
    return {Kind::Paste, Kind::Copy, Kind::Pin, Kind::Unpin, Kind::Delete};
}

} // namespace RunnerActions
