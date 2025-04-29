#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include <mega/common/database_forward.h>
#include <mega/common/query_forward.h>

namespace mega
{
namespace common
{

struct DatabaseVersion
{
    // Called to undo the actions of mUpgrade.
    std::function<void(Query&)> mDowngrade;

    // Called to perform changes necessary for a given version.
    std::function<void(Query&)> mUpgrade;
}; // DatabaseVersion

// Convenience.
using DatabaseVersionVector =
  std::vector<DatabaseVersion>;

class DatabaseBuilder
{
    // What versions exist for this database?
    virtual const DatabaseVersionVector& versions() const = 0;

    // Downgrade the database to the specified version.
    void downgrade(const DatabaseVersionVector& versions,
                   std::size_t target);

    // Upgrade the database to the specified version.
    void upgrade(const DatabaseVersionVector& versions,
                 std::size_t target);

    // What database are we operating on?
    Database& mDatabase;

protected:
    explicit DatabaseBuilder(Database& database);

    ~DatabaseBuilder() = default;

public:
    // Create or update the database to the latest version.
    void build();

    // Downgrade the database to the specified version.
    void downgrade(std::size_t target);

    // Upgrade the database to the specified version.
    void upgrade(std::size_t target);
}; // DatabaseBuilder

} // common
} // mega

