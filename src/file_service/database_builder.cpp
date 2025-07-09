#include <mega/common/query.h>
#include <mega/common/utility.h>
#include <mega/file_service/database_builder.h>

namespace mega
{
namespace file_service
{

using namespace common;

static void downgrade10(Query& query);

static void upgrade01(Query& query);

const DatabaseVersionVector& DatabaseBuilder::versions() const
{
    static const DatabaseVersionVector versions = {
        {&downgrade10, &upgrade01},
    }; // versions

    return versions;
}

DatabaseBuilder::DatabaseBuilder(Database& database):
    common::DatabaseBuilder(database)
{}

void downgrade10(Query& query)
{
    static const char* tables[] = {"file_id", "file_ids", "file_ranges", "files"}; // tables

    for (const auto* table: tables)
    {
        query = format("drop table %s", table);
        query.execute();
    }
}

void upgrade01(Query& query)
{
    query = "create table files ( "
            "  accessed integer "
            "  constraint nn_files_accessed "
            "             not null, "
            "  dirty integer "
            "  constraint nn_files_dirty "
            "             not null, "
            "  handle integer "
            "  constraint uq_files_handle "
            "             unique, "
            "  id integer "
            "  constraint nn_files_id "
            "             not null, "
            "  modified integer "
            "  constraint nn_files_modified "
            "             not null, "
            "  num_references integer "
            "  constraint nn_files_num_references "
            "             not null, "
            "  constraint pk_files "
            "             primary key (id) "
            ")";

    query.execute();

    query = "create table file_ranges ( "
            "  begin integer "
            "  constraint nn_file_ranges_begin "
            "             not null, "
            "  end integer "
            "  constraint nn_file_ranges_end "
            "             not null, "
            "  id integer "
            "  constraint nn_file_ranges_id "
            "             not null, "
            "  constraint fk_file_ranges_files "
            "             foreign key (id) "
            "             references files (id) "
            "             on delete cascade, "
            "  constraint pk_file_ranges "
            "             primary key (begin, id), "
            "  constraint uq_file_ranges_end_id "
            "             unique (end, id) "
            ")";

    query.execute();

    query = "create table file_ids ( "
            "  id integer "
            "  constraint nn_file_ids_id "
            "             not null, "
            "  constraint pk_file_ids "
            "             primary key (id) "
            ")";

    query.execute();

    query = "create table file_id ( "
            "  id integer "
            "  constraint nn_file_id_id "
            "             not null, "
            "  next integer "
            "  constraint nn_file_id_next "
            "             not null, "
            "  constraint pk_file_id "
            "             primary key (id) "
            ")";

    query.execute();

    query = "insert into file_id values (0, 0)";

    query.execute();
}

} // file_service
} // mega
