#pragma once

#include <QString>

#include <optional>

// Composable query filter for the history list. Applied on the SQL side.
struct FilterSpec {
    QString searchText; // case-insensitive substring over preview/text
    int contentType = -1; // -1 = all, else ContentType value
    qint64 fromMs = 0; // 0 = unbounded
    qint64 toMs = 0; // 0 = unbounded
    QString sourceApp; // empty = all
    std::optional<qint64> groupId; // entries assigned to this group
    bool pinnedOnly = false;

    bool isTrivial() const
    {
        return searchText.isEmpty() && contentType == -1 && fromMs == 0 && toMs == 0
            && sourceApp.isEmpty() && !groupId.has_value() && !pinnedOnly;
    }
};
