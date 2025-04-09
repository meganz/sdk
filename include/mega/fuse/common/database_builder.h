#pragma once

#include <mega/common/database_builder.h>
#include <mega/common/database_forward.h>

namespace mega
{
namespace fuse
{

class DatabaseBuilder
  : public common::DatabaseBuilder
{
    // What versions exist for this database?
    const common::DatabaseVersionVector& versions() const override;

public:
    explicit DatabaseBuilder(common::Database& database);
}; // DatabaseBuilder

} // fuse
} // mega

