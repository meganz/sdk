#pragma once

#include <mega/common/database_builder.h>
#include <mega/common/database_utilities.h>

namespace mega
{

class LocalPath;

namespace file_service
{

class DatabaseBuilder: public common::DatabaseBuilder
{
    auto versions() const -> const common::DatabaseVersionVector& override;

    // Temporarily disables foreign keys while working on the database.
    //
    // This is necessary to prevent SQLite from removing entries in tables
    // such as file_ranges when adding (or removing) fields from the files
    // table.
    common::ScopedForeignKeyDisabler mDisabler;

public:
    explicit DatabaseBuilder(common::Database& database);
}; // DatabaseBuilder

} // file_service
} // mega
