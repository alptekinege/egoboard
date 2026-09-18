#pragma once

#include "FilterSpec.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

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
    // Free text keeps quoted phrases ("one two"), supports -exclusions, NOT
    // before a term, uppercase OR between alternatives, and /regex/ patterns.
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
    // The scope restricts the match to one FTS column (preview / text_data /
    // ocr_text); All searches every indexed column.
    // Empty input returns empty string (caller should fall back to no filter).
    static QString buildFtsQuery(const QString &userText,
                                 FilterSpec::SearchScope scope = FilterSpec::SearchScope::All);

    // Include terms grouped by OR: every term of a group must be present (AND),
    // the groups are alternatives. Quotes and operators are stripped, so this is
    // what the LIKE fallback and the SQL builder consume. Uppercase AND/OR/NOT
    // are operators; lowercase words stay search terms.
    static QVector<QStringList> orGroups(const QString &userText);

    // Plain highlight terms: orGroups() flattened and de-duplicated.
    static QStringList textTerms(const QString &userText);

    // Turns an excludeText expression into search-box syntax: draft "not now"
    // becomes -draft -"not now". Used when restoring a saved search.
    static QString negatedTerms(const QString &excludeText);

    // Lightweight LIKE escape helper (mirrors StorageManager::likeEscape).
    static QString likeEscape(const QString &text);

    // FTS5 expression for one word or quoted phrase ("hello"* / "one two"*);
    // empty when the term has no searchable characters. Used to apply each
    // exclusion term independently.
    static QString buildFtsTerm(const QString &term);
};
