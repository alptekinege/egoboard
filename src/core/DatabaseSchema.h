#pragma once

#include <QSqlDatabase>

namespace DatabaseSchema {

// Creates all tables/indexes and applies connection pragmas. Idempotent.
bool ensure(QSqlDatabase &db);

} // namespace DatabaseSchema
