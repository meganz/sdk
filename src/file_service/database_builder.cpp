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

auto DatabaseBuilder::versions() const -> const DatabaseVersionVector&
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
    static const char* tables[] = {"file_id", "file_ids", "file_chunks", "files"}; // tables

    for (const auto* table: tables)
    {
        query = format("drop table %s", table);
        query.execute();
    }
}

void upgrade01(Query& query)
{
    query = "create table files ( "
            "  handle integer "
            "  constraint uq_files_handle "
            "             unique, "
            "  id integer "
            "  constraint nn_files_id "
            "             not null, "
            "  num_references integer "
            "  constraint nn_files_num_references "
            "             not null, "
            "  constraint pk_files "
            "             primary key (id) "
            ")";

    query.execute();

    query = "create table file_chunks ( "
            "  begin integer "
            "  constraint nn_file_chunks_begin "
            "             not null, "
            "  end integer "
            "  constraint nn_file_chunks_end "
            "             not null, "
            "  id integer "
            "  constraint nn_file_chunks_id "
            "             not null, "
            "  constraint fk_file_chunks_files "
            "             foreign key (id) "
            "             references files (id) "
            "             on delete cascade, "
            "  constraint pk_file_chunks "
            "             primary key (begin, id), "
            "  constraint uq_file_chunks_end_id "
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
            "  free integer, "
            "  id integer "
            "  constraint nn_file_id_id "
            "             not null, "
            "  next integer "
            "  constraint nn_file_id_next "
            "             not null, "
            "  constraint fk_file_id_file_ids "
            "             foreign key (free) "
            "             references file_ids (id) "
            "  constraint pk_file_id "
            "             primary key (id) "
            ")";

    query.execute();

    query = "insert into file_id values (null, 0, 0)";

    query.execute();
}

} // file_service
} // mega
