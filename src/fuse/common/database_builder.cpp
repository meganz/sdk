#include <algorithm>
#include <cassert>
#include <vector>

#include <mega/fuse/common/database_builder.h>
#include <mega/fuse/common/database.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/query.h>
#include <mega/fuse/common/scoped_query.h>
#include <mega/fuse/common/transaction.h>

namespace mega
{
namespace fuse
{

using DowngradeFunction = void (*)(Query&);
using UpgradeFunction = void (*)(Query&);

static std::size_t currentVersion(Query& query);

static void downgrade10(Query& query);
static void downgrade21(Query& query);
static void downgrade32(Query& query);

static void upgrade01(Query& query);
static void upgrade12(Query& query);
static void upgrade23(Query& query);

static const std::vector<DowngradeFunction> downgrades = {
    nullptr,
    &downgrade10,
    &downgrade21,
    &downgrade32,
}; // downgrades

static const std::vector<UpgradeFunction> upgrades = {
    &upgrade01,
    &upgrade12,
    &upgrade23,
}; // upgrades

template<typename Function>
void DatabaseBuilder::withQuery(Function&& function)
{
    auto databaseLock = DatabaseLock(mDatabase);
    auto transaction = mDatabase.transaction();
    auto query = mDatabase.query();

    function(query);

    transaction.commit();
}

DatabaseBuilder::DatabaseBuilder(Database& database)
  : mDatabase(database)
{
}

void DatabaseBuilder::build()
{
    assert(downgrades.size() == upgrades.size() + 1);
    assert(!upgrades.empty());

    upgrade(upgrades.size());
}

void DatabaseBuilder::downgrade(std::size_t target)
{
    withQuery([&](Query& query) {
        auto current = currentVersion(query);

        if (current <= target)
            return;

        for (; current > target; --current)
        {
            auto* downgrader = &downgrades[current];

            assert(*downgrader);

            FUSEDebugF("Downgrading database to version %zu", current - 1);

            (*downgrader)(query);

            query = "delete from version where version = :version";

            query.param(":version") = current;
            query.execute();
        }
    });
}

void DatabaseBuilder::upgrade(std::size_t target)
{
    target = std::min(target, upgrades.size());

    withQuery([&](Query& query) {
        auto current = currentVersion(query);

        if (current >= target)
            return;

        while (current < target)
        {
            auto* upgrader = &upgrades[current++];

            assert(*upgrader);

            FUSEDebugF("Upgrading database to version %zu", current);

            (*upgrader)(query);

            query = "insert into version values (:version)";

            query.param(":version").uint64(current);
            query.execute();
        }
    });
}

static std::size_t currentVersion(Query& query)
{
    // Check if the "version" table exists.
    query = "select name "
            "  from sqlite_master "
            " where name = :name "
            "   and type = :type";

    query.param(":name") = "version";
    query.param(":type") = "table";
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

    return query.field("version").uint64();
}

void downgrade10(Query& query)
{
    query = "drop table inode_id";
    query.execute();

    query = "drop table inodes";
    query.execute();

    query = "drop table mounts";
    query.execute();

    query = "drop table versions";
    query.execute();
}

void downgrade21(Query& query)
{
    query = "alter table inodes drop column bind_handle";
    query.execute();
}

void downgrade32(Query& query)
{
    query = "alter table drop column extension";
    query.execute();
}

void upgrade01(Query& query)
{
    // Tracks all inodes with local state.
    query = "create table inodes ( "
            "  handle integer "
            "  constraint uq_inodes_handle "
            "             unique, "
            "  id integer "
            "  constraint nn_inodes_id "
            "             not null, "
            "  modified integer "
            "  constraint nn_inodes_modified "
            "             not null, "
            "  name text, "
            "  parent_handle integer, "
            "  constraint pk_inodes "
            "             primary key (id), "
            "  constraint uq_inodes_name_parent_handle "
            "             unique (name, parent_handle) "
            ")";

    query.execute();

    // Records what the next synthetic inode ID should be.
    query = "create table inode_id ( "
            "  next integer "
            "  constraint nn_inode_id_next "
            "             not null, "
            "  constraint pk_inode_id "
            "             primary key (next) "
            ")";

    query.execute();

    // Initially, no IDs have been allocated.
    query = "insert into inode_id values (:value)";

    query.param(":value") = InodeID(0);
    query.execute();

    // Tracks all mounts known by FUSE.
    query = "create table mounts ( "
            "  enable_at_startup integer "
            "  constraint nn_mounts_enable_at_startup "
            "             not null, "
            "  id integer "
            "  constraint nn_mounts_id "
            "             not null, "
            "  name text "
            "  constraint nn_mounts_name "
            "             not null, "
            "  path text "
            "  constraint nn_mounts_path "
            "             not null, "
            "  persistent integer "
            "  constraint nn_mounts_persistent "
            "             not null, "
            "  read_only integer "
            "  constraint nn_mounts_read_only "
            "             not null, "
            "  constraint pk_mounts "
            "             primary key (path) "
            ")";

    query.execute();

    // Tracks which datahbase upgrades have been performed.
    query = "create table version ( "
            "  version integer "
            "  constraint nn_version_version "
            "             not null, "
            "  constraint pk_versions "
            "             primary key (version) "
            ")";

    query.execute();
}

void upgrade12(Query& query)
{
    // Add a new inodes table with a bind_handle column.
    query = "create table inodes_new ( "
            "  bind_handle text "
            "  constraint uq_inodes_bind_handle "
            "             unique, "
            "  handle integer "
            "  constraint uq_inodes_handle "
            "             unique, "
            "  id integer "
            "  constraint nn_inodes_id "
            "             not null, "
            "  modified integer "
            "  constraint nn_inodes_modified "
            "             not null, "
            "  name text, "
            "  parent_handle integer, "
            "  constraint pk_inodes "
            "             primary key (id), "
            "  constraint uq_inodes_name_parent_handle "
            "             unique (name, parent_handle) "
            ")";

    query.execute();

    // Migrate existing inode data to our new table.
    query = "insert into inodes_new "
            "select null "
            "     , handle "
            "     , id "
            "     , modified "
            "     , name "
            "     , parent_handle "
            "  from inodes";

    query.execute();

    // Replace the old inodes table with our new one.
    query = "drop table inodes";
    query.execute();

    query = "alter table inodes_new rename to inodes";
    query.execute();
}

void upgrade23(Query& query)
{
    // Add a new inodes table with an extension column.
    query = "create table inodes_new ( "
            "  bind_handle text "
            "  constraint uq_inodes_bind_handle "
            "             unique, "
            "  extension text, "
            "  handle integer "
            "  constraint uq_inodes_handle "
            "             unique, "
            "  id integer "
            "  constraint nn_inodes_id "
            "             not null, "
            "  modified integer "
            "  constraint nn_inodes_modified "
            "             not null, "
            "  name text, "
            "  parent_handle integer, "
            "  constraint pk_inodes "
            "             primary key (id), "
            "  constraint uq_inodes_name_parent_handle "
            "             unique (name, parent_handle) "
            ")";

    query.execute();

    // Migrate existing inode data to our new table.
    query = "insert into inodes_new "
            "select bind_handle "
            "     , null "
            "     , handle "
            "     , id "
            "     , modified "
            "     , name "
            "     , parent_handle "
            "  from inodes";

    query.execute();

    // Replace the old inodes table with our new one.
    query = "drop table inodes";
    query.execute();

    query = "alter table inodes_new rename to inodes";
    query.execute();
}

} // fuse
} // mega

