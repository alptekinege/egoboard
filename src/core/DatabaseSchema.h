#pragma once

#include <QSqlDatabase>

namespace DatabaseSchema {

// Creates all tables/indexes and applies connection pragmas. Idempotent.
bool ensure(QSqlDatabase &db);

// PRAGMA quick_check: true when the database reports "ok". A failed check
// leaves *error at the first reported problem.
bool quickCheck(QSqlDatabase &db, QString *error = nullptr);
// Rebuilds the FTS index from the entries table; repairs a stale or damaged
// search index without touching the history itself.
bool rebuildSearchIndex(QSqlDatabase &db);

bool isSqlCipherAvailable(QSqlDatabase &db);
bool probeKey(QSqlDatabase &db);
bool setKey(QSqlDatabase &db, const QString &key);
bool rekey(QSqlDatabase &db, const QString &newKey);
QString cipherVersion(QSqlDatabase &db);

} // namespace DatabaseSchema
