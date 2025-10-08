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
            "  allocated_size integer "
            "  constraint nn_files_allocated_size "
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
            "  name text, "
            "  parent_handle integer, "
            "  removed integer "
            "  constraint nn_files_removed "
            "             not null, "
            "  reported_size integer "
            "  constraint nn_files_reported_size "
            "             not null, "
            "  size integer "
            "  constraint nn_files_size "
            "             not null, "
            "  constraint pk_files "
            "             primary key (id), "
            "  constraint uq_files_handle "
            "             unique (handle), "
            "  constraint uq_files_name_parent_handle "
            "             unique (name, parent_handle) "
            ")";

    query.execute();

    query = "create table file_key_data ( "
            "  chat_auth text, "
            "  id integer "
            "  constraint nn_file_key_data_id "
            "             not null, "
            "  is_public integer "
            "  constraint nn_file_key_data_is_public "
            "             not null, "
            "  key_and_iv text "
            "  constraint nn_file_key_data_key_and_iv "
            "             not null, "
            "  public_auth text, "
            "  private_auth text, "
            "  constraint fk_file_key_data_files "
            "             foreign key (id) "
            "             references files (id) "
            "             on delete cascade, "
            "  constraint pk_file_key_data "
            "            primary key (id) "
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
