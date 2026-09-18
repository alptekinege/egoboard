#pragma once

#include "FilterSpec.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

// Small, GUI-free helper for building fast full-text queries.
// Lives in egoboard_core so unit tests can exercise it headless.
class SearchEngine {
public:
    // Typed query parsed from the search box. Field filters:
    //   app:firefox        source application (exact, quoted for spaces)
    //   type:image         text | richtext | image | files
    //   tag:work           may be repeated; entries must carry all of them
    //   pinned:yes|no      pinned only / exclude nothing
    //   sensitive:yes|no   audit view
    //   has:ocr            only entries with OCR text
    //   before:2024-01-31  also today|yesterday|30m|12h|3d|2w
    //   after:2024-01-01   same value forms
    // Free text keeps quoted phrases ("one two") and supports -exclusions.
    struct ParsedQuery {
        FilterSpec filter; // base filter plus every parsed field filter
        QString text; // free text, quotes kept so phrases stay phrases
        QStringList applied; // human-readable applied filters, for the UI hint
        QStringList problems; // values that could not be used (shown as a hint)
    };

    // Splits raw input into field filters, free text and -exclusions. Fields
    // the user typed override the matching fields of `base`; everything else
    // keeps the toolbar value.
    static ParsedQuery parseQuery(const QString &input, const FilterSpec &base = {});

    // Returns true when the entries_fts virtual table exists and is usable.
    static bool isFtsAvailable(const QSqlDatabase &db);

    // Converts raw user input ("hello world") into an FTS5 MATCH expression
    // like:  "\"hello\"* AND \"world\"*"
    // Quoted phrases become proper FTS5 phrases: "one two" -> "one two"*
    // Empty input returns empty string (caller should fall back to no filter).
    static QString buildFtsQuery(const QString &userText);

    // The user text without FTS quoting, for the LIKE fallback and for display.
    static QString stripQueryQuotes(const QString &userText);

    // Turns an excludeText expression into search-box syntax: draft "not now"
    // becomes -draft -"not now". Used when restoring a saved search.
    static QString negatedTerms(const QString &excludeText);

    // Lightweight LIKE escape helper (mirrors StorageManager::likeEscape).
    static QString likeEscape(const QString &text);

private:
    static QString escapeFtsToken(QString token);
};
