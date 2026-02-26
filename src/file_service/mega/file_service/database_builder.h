#pragma once

#include <mega/common/database_builder.h>

namespace mega
{

class LocalPath;

namespace file_service
{

class DatabaseBuilder: public common::DatabaseBuilder
{
    auto versions() const -> const common::DatabaseVersionVector& override;

public:
    explicit DatabaseBuilder(common::Database& database);
}; // DatabaseBuilder

} // file_service
} // mega
