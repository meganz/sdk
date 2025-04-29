#include <algorithm>
#include <cassert>
#include <map>
#include <vector>

#include <mega/common/database.h>
#include <mega/common/database_utilities.h>
#include <mega/common/logging.h>
#include <mega/common/query.h>
#include <mega/common/transaction.h>
#include <mega/fuse/common/database_builder.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/mount_info.h>

namespace mega
{
namespace fuse
{

using namespace common;

static void downgrade10(Query& query);
static void downgrade21(Query& query);
static void downgrade32(Query& query);
static void downgrade43(Query& query);

static void upgrade01(Query& query);
static void upgrade12(Query& query);
static void upgrade23(Query& query);
static void upgrade34(Query& query);

const DatabaseVersionVector& DatabaseBuilder::versions() const
{
    static const DatabaseVersionVector versions = {
        {&downgrade10, &upgrade01},
        {&downgrade21, &upgrade12},
        {&downgrade32, &upgrade23},
        {&downgrade43, &upgrade34},
    }; // versions

    return versions;
}

DatabaseBuilder::DatabaseBuilder(Database& database)
  : common::DatabaseBuilder(database)
{
}

void downgrade10(Query& query)
{
    query = "drop table inode_id";
    query.execute();

    query = "drop table inodes";
    query.execute();

    query = "drop table mounts";
    query.execute();
}

void downgrade21(Query& query)
{
    // Inode table loses its bind_handle field.
    query = "create table inodes_new ( "
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
            "select handle "
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

void downgrade32(Query& query)
{
    query = "alter table inodes drop column extension";
    query.execute();
}

void downgrade43(Query& query)
{
    // Tracks which mounts we're migrating.
    MountInfoSet<MountInfoPathLess> mounts;

    // Mounts become keyed on path rather than name.
    //
    // Mounts with the same path will be dropped.
    query = "create table mounts_new ( "
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

    // Figure out which mounts we want to migrate.
    query = "select * from mounts";

    for (query.execute(); query; ++query)
    {
        // Add a mount to the set only if it has a path.
        if (!query.field("path").null())
            mounts.emplace(MountInfo::deserialize(query));
    }

    // Add mounts back into the database.
    query = "insert into mounts_new values ( "
            "  :enable_at_startup, "
            "  :id, "
            "  :name, "
            "  :path, "
            "  :persistent, "
            "  :read_only "
            ")";

    for (auto i = mounts.begin(); i != mounts.end(); ++i)
    {
        // Reset any parameter bindings.
        query.reset();

        // Write mount into the database.
        i->serialize(query);

        // Add the mount.
        query.execute();
    }

    // Replace the old mounts table with our new one.
    query = "drop table mounts";
    query.execute();

    query = "alter table mounts_new rename to mounts";
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

    query.param(":value").set(InodeID(0));
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

void upgrade34(Query& query)
{
    // The mounts table will now be keyed on name.
    query = "create table mounts_new ( "
            "  enable_at_startup integer "
            "  constraint nn_mounts_enable_at_startup "
            "             not null, "
            "  id integer "
            "  constraint nn_mounts_id "
            "             not null, "
            "  name text "
            "  constraint nn_mounts_name "
            "             not null, "
            "  path text, "
            "  persistent integer "
            "  constraint nn_mounts_persistent "
            "             not null, "
            "  read_only integer "
            "  constraint nn_mounts_read_only "
            "             not null, "
            "  constraint pk_mounts "
            "             primary key (name) "
            ")";

    // Create the new table.
    query.execute();

    // Tracks the mounts we are migrating.
    std::vector<MountInfo> mounts;

    // Tracks how many times a given name has been used.
    std::map<std::string, std::size_t> occurances;

    // Migrate existing mounts, resolving name conflicts as necessary.
    query = "select * from mounts";

    for (query.execute(); query; ++query)
    {
        // Latch the mount's description.
        mounts.emplace_back(MountInfo::deserialize(query));

        // Convenience.
        auto& name = mounts.back().mFlags.mName;

        // Remember that we've seen this mount's name.
        auto i = occurances.emplace(name, 0u);

        // We've seen this name before.
        while (!i.second)
        {
            // generate a new name by adding a counter.
            name += " (" + std::to_string(++i.first->second) + ")";

            // make sure our new name is unique.
            i = occurances.emplace(name, 0u);
        }
    }

    // Add mounts back into the database.
    query = "insert into mounts_new values ( "
            "  :enable_at_startup, "
            "  :id, "
            "  :name, "
            "  :path, "
            "  :persistent, "
            "  :read_only "
            ")";

    while (!mounts.empty())
    {
        // Reset any bound query parameters.
        query.reset();

        // Write the mount's info to the database.
        mounts.back().serialize(query);

        // Add the mount.
        query.execute();

        // Mount's been added.
        mounts.pop_back();
    }

    // Replace the old mounts table with our new one.
    query = "drop table mounts";
    query.execute();

    query = "alter table mounts_new rename to mounts";
    query.execute();
}

} // fuse
} // mega

