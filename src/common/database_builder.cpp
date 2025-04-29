#include <cassert>
#include <limits>

#include <mega/common/database_builder.h>
#include <mega/common/database_utilities.h>
#include <mega/common/logging.h>

namespace mega
{
namespace common
{

static std::size_t currentVersion(Query& query);

void DatabaseBuilder::downgrade(const DatabaseVersionVector& versions,
                                std::size_t target)
{
    withQuery(mDatabase, [&](Query&& query) {
        // What version are we at?
        auto current = currentVersion(query);

        // Already at or below the target version.
        if (current <= target)
            return;

        // Downgrade to the target version.
        for (; current > target; --current)
        {
            const auto& version = versions[current - 1];

            // Sanity.
            assert(version.mDowngrade);

            LogDebugF(query.logger(),
                      "Downgrading database to version %zu",
                      current - 1);

            version.mDowngrade(query);

            query = "delete from version where version = :version";

            query.param(":version").set(current);
            query.execute();
        }
    });
}

void DatabaseBuilder::upgrade(const DatabaseVersionVector& versions,
                              std::size_t target)
{
    target = std::min(target, versions.size());

    withQuery(mDatabase, [&](Query&& query) {
        // What version are we at?
        auto current = currentVersion(query);

        // Already at or above the target version.
        if (current >= target)
            return;

        // Make sure the database has a version table.
        if (!current)
        {
            // Tracks which datahbase upgrades have been performed.
            query = "create table if not exists version ( "
                    "  version integer "
                    "  constraint nn_version_version "
                    "             not null, "
                    "  constraint pk_version "
                    "             primary key (version) "
                    ")";

            query.execute();
        }

        // Upgrade to the target version.
        while (current < target)
        {
            auto& version = versions[current++];

            // Sanity.
            assert(version.mUpgrade);

            LogDebugF(query.logger(),
                      "Upgrading database to version %zu",
                      current);

            version.mUpgrade(query);

            query = "insert into version values (:version)";

            query.param(":version").set(current);
            query.execute();
        }
    });
}

DatabaseBuilder::DatabaseBuilder(Database& database)
  : mDatabase(database)
{
}

void DatabaseBuilder::build()
{
    upgrade(versions(), std::numeric_limits<std::size_t>::max());
}

void DatabaseBuilder::downgrade(std::size_t target)
{
    downgrade(versions(), target);
}

void DatabaseBuilder::upgrade(std::size_t target)
{
    upgrade(versions(), target);
}

std::size_t currentVersion(Query& query)
{
    // Check if the "version" table exists.
    query = "select name "
            "  from sqlite_master "
            " where name = :name "
            "   and type = :type";

    query.param(":name").set("version");
    query.param(":type").set("table");
    query.execute();

    // Table doesn't exist: Must be the initial version.
    if (!query)
        return 0u;

    // Determine the database's current version.
    query = "   select version "
            "     from version "
            " order by version desc "
            "    limit 1";

    query.execute();

    // There's at least one version in the table.
    if (query)
        return query.field("version").get<std::size_t>();

    // No versions yet: Must be the initial version.
    return 0u;
}

} // common
} // mega

