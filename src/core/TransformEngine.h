#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

/**
 * @brief Pure, headless transform engine for clipboard text.
 *
 * Chainable, local-only text transforms. No file or network access, no GUI
 * dependency — fully unit-testable via QTEST_GUILESS_MAIN.
 */
class TransformEngine {
public:
    enum class TransformId : int {
        Trim = 0,
        Uppercase,
        Lowercase,
        Capitalize,
        Reverse,
        Base64Encode,
        Base64Decode,
        UrlEncode,
        UrlDecode,
        JsonPretty,
        JsonMinify,
        HtmlEscape,
        HtmlUnescape,
        SortLines,
        UniqueLines,
        RemoveEmptyLines,
        TrimLines,
    };

    struct Result {
        bool ok = true;
        QString output;
        QString error;
    };

    struct Descriptor {
        TransformId id;
        QString name; // stable, lowercase, e.g. "json-pretty"
        QString label; // human, e.g. "JSON Pretty"
        QString description;
    };

    static QVector<Descriptor> allDescriptors();
    static QVector<TransformId> allIds();
    static Descriptor descriptor(TransformId id);
    static QString nameForId(TransformId id);
    static QString labelForId(TransformId id);
    static QString descriptionForId(TransformId id);
    static std::optional<TransformId> idForName(const QString &name);

    static Result apply(TransformId id, const QString &input);
    static Result applyChain(const QString &input, const QList<TransformId> &chain);
    static Result applyChainByNames(const QString &input, const QStringList &names, QString *failedName = nullptr);
};
