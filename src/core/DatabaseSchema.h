#pragma once

#include <QSqlDatabase>

namespace DatabaseSchema {

// Creates all tables/indexes and applies connection pragmas. Idempotent.
bool ensure(QSqlDatabase &db);

bool isSqlCipherAvailable(QSqlDatabase &db);
bool probeKey(QSqlDatabase &db);
bool setKey(QSqlDatabase &db, const QString &key);
bool rekey(QSqlDatabase &db, const QString &newKey);
QString cipherVersion(QSqlDatabase &db);

} // namespace DatabaseSchema
