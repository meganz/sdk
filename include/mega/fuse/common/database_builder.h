#pragma once

#include <cstddef>

#include <mega/fuse/common/database_forward.h>
#include <mega/fuse/common/query.h>

namespace mega
{
namespace fuse
{

class DatabaseBuilder
{
    template<typename Function>
    void withQuery(Function&& function);

    Database& mDatabase;

public:
    DatabaseBuilder(Database& database);

    // Create or update the database to the latest version.
    void build();

    // Downgrade the database to the specified version.
    void downgrade(std::size_t target);

    // Upgrade the database to the specified version.
    void upgrade(std::size_t target);
}; // DatabaseBuilder

} // fuse
} // mega

