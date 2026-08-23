#pragma once

#include <QSqlDatabase>
#include <QString>

// Small, GUI-free helper for building fast full-text queries.
// Lives in egoboard_core so unit tests can exercise it headless.
class SearchEngine {
public:
    // Returns true when the entries_fts virtual table exists and is usable.
    static bool isFtsAvailable(const QSqlDatabase &db);

    // Converts raw user input ("hello world") into an FTS5 MATCH expression
    // like:  "\"hello\"* AND \"world\"*"
    // Empty input returns empty string (caller should fall back to no filter).
    // Every token is quoted and prefix-matched, so typing is responsive.
    static QString buildFtsQuery(const QString &userText);

    // Lightweight LIKE escape helper (mirrors StorageManager::likeEscape).
    static QString likeEscape(const QString &text);

private:
    static QString escapeFtsToken(QString token);
};
