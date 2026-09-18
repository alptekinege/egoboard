#pragma once

#include <QString>
#include <QStringList>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <optional>

// Composable query filter for the history list. Applied on the SQL side.
struct FilterSpec {
    enum class SortMode : int {
        Newest = 0, // timestamp descending (default)
        Oldest = 1, // timestamp ascending
        MostUsed = 2, // use count descending, newest first on ties
    };

    // Which text the search matches: everything indexed, the list preview, the
    // stored payload or the OCR output.
    enum class SearchScope : int {
        All = 0,
        Preview = 1,
        FullText = 2,
        Ocr = 3,
    };

    QString searchText; // free text (quoted phrases kept); case-insensitive over preview/text/OCR
    QString excludeText; // "-term" / -"phrase": entries matching any of these are dropped
    QString regexText; // "/pattern/": QT regular expression over the search scope
    int contentType = -1; // -1 = all, else ContentType value
    qint64 fromMs = 0; // 0 = unbounded
    qint64 toMs = 0; // 0 = unbounded
    QString sourceApp; // empty = all
    std::optional<qint64> groupId; // entries assigned to this group
    bool pinnedOnly = false;
    bool sensitiveOnly = false; // audit view: only entries flagged sensitive
    bool hasOcrOnly = false; // only entries with OCR text
    QStringList tags; // entry must carry ALL of these tags
    SortMode sortMode = SortMode::Newest;
    SearchScope searchScope = SearchScope::All; // ignored when searchText is empty

    bool isTrivial() const
    {
        return searchText.isEmpty() && excludeText.isEmpty() && regexText.isEmpty()
            && contentType == -1 && fromMs == 0 && toMs == 0 && sourceApp.isEmpty()
            && !groupId.has_value() && !pinnedOnly && !sensitiveOnly && !hasOcrOnly
            && tags.isEmpty() && sortMode == SortMode::Newest;
    }

    // JSON codec (used to persist saved searches). Unknown keys are ignored so
    // older saved searches survive schema growth.
    QJsonObject toJson() const
    {
        QJsonObject json;
        if (!searchText.isEmpty())
            json.insert(QStringLiteral("searchText"), searchText);
        if (!excludeText.isEmpty())
            json.insert(QStringLiteral("excludeText"), excludeText);
        if (!regexText.isEmpty())
            json.insert(QStringLiteral("regexText"), regexText);
        if (contentType >= 0)
            json.insert(QStringLiteral("contentType"), contentType);
        if (fromMs > 0)
            json.insert(QStringLiteral("fromMs"), static_cast<qint64>(fromMs));
        if (toMs > 0)
            json.insert(QStringLiteral("toMs"), static_cast<qint64>(toMs));
        if (!sourceApp.isEmpty())
            json.insert(QStringLiteral("sourceApp"), sourceApp);
        if (groupId.has_value())
            json.insert(QStringLiteral("groupId"), groupId.value());
        if (pinnedOnly)
            json.insert(QStringLiteral("pinnedOnly"), true);
        if (sensitiveOnly)
            json.insert(QStringLiteral("sensitiveOnly"), true);
        if (hasOcrOnly)
            json.insert(QStringLiteral("hasOcrOnly"), true);
        if (!tags.isEmpty()) {
            QJsonArray tagArray;
            for (const QString &tag : tags)
                tagArray.append(tag);
            json.insert(QStringLiteral("tags"), tagArray);
        }
        if (sortMode != SortMode::Newest)
            json.insert(QStringLiteral("sortMode"), static_cast<int>(sortMode));
        if (searchScope != SearchScope::All)
            json.insert(QStringLiteral("searchScope"), static_cast<int>(searchScope));
        return json;
    }

    static FilterSpec fromJson(const QJsonObject &json)
    {
        FilterSpec filter;
        filter.searchText = json.value(QLatin1String("searchText")).toString();
        filter.excludeText = json.value(QLatin1String("excludeText")).toString();
        filter.regexText = json.value(QLatin1String("regexText")).toString();
        filter.contentType = json.value(QLatin1String("contentType")).toInt(-1);
        filter.fromMs = json.value(QLatin1String("fromMs")).toInteger();
        filter.toMs = json.value(QLatin1String("toMs")).toInteger();
        filter.sourceApp = json.value(QLatin1String("sourceApp")).toString();
        if (json.contains(QLatin1String("groupId")))
            filter.groupId = json.value(QLatin1String("groupId")).toInteger();
        filter.pinnedOnly = json.value(QLatin1String("pinnedOnly")).toBool();
        filter.sensitiveOnly = json.value(QLatin1String("sensitiveOnly")).toBool();
        filter.hasOcrOnly = json.value(QLatin1String("hasOcrOnly")).toBool();
        const QJsonArray tagArray = json.value(QLatin1String("tags")).toArray();
        for (const QJsonValue &value : tagArray) {
            const QString tag = value.toString();
            if (!tag.isEmpty())
                filter.tags.append(tag);
        }
        const int sort = json.value(QLatin1String("sortMode")).toInt(0);
        if (sort >= 0 && sort <= static_cast<int>(SortMode::MostUsed))
            filter.sortMode = static_cast<SortMode>(sort);
        const int scope = json.value(QLatin1String("searchScope")).toInt(0);
        if (scope >= 0 && scope <= static_cast<int>(SearchScope::Ocr))
            filter.searchScope = static_cast<SearchScope>(scope);
        return filter;
    }

    QString toJsonString() const
    {
        return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    }

    static FilterSpec fromJsonString(const QString &json)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        return doc.isObject() ? fromJson(doc.object()) : FilterSpec{};
    }
};
