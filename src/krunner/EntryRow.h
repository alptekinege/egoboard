#pragma once

#include "ContentType.h"

#include <QString>
#include <QStringList>

/**
 * @brief The detailed entry row shared by the D-Bus adaptor and the KRunner
 * plugin.
 *
 * Both sides live in different processes (and different libraries), so the wire
 * format is defined once here: one tab-separated row per entry
 *
 *   id \t type \t sourceApp \t sourceWindow \t pinned(0/1) \t timestampMs \t preview
 *
 * `preview` is the last field, keeps its newlines (KRunner shows them) and is
 * capped. Tabs inside the payload are replaced, so splitting on tabs is safe.
 * Decoding tolerates extra fields, so a newer adaptor can append more without
 * breaking an older plugin.
 */
struct EntryRow {
    qint64 id = 0;
    QString type; // "text" | "html" | "image" | "files"
    QString sourceApp;
    QString sourceWindow;
    bool pinned = false;
    qint64 timestamp = 0;
    QString preview;

    bool isValid() const { return id > 0; }

    static constexpr int kMaxPreviewChars = 300;

    // Short id of a content type ("text", "html", "image", "files").
    static QString contentTypeId(ContentType type);

    QString encode() const;
    // Returns an invalid row (id 0) when the line does not look like an entry.
    static EntryRow decode(const QString &line);

    // "Firefox · text" — empty parts are left out.
    QString summary() const;

    // KRunner result category for this row's content type: plain and rich
    // text share "text", images and files group on their own. Pure over the
    // type id so the grouping is unit-testable without KRunner itself.
    QString matchCategory() const;
    static QString categoryForType(const QString &typeId);
};
