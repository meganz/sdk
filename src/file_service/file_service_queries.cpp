#include <mega/common/database.h>
#include <mega/file_service/file_service_queries.h>

namespace mega
{
namespace file_service
{

using namespace common;

FileServiceQueries::FileServiceQueries(Database& database):
    mAddFile(database.query()),
    mGetFile(database.query())
{
    mAddFile = "insert into files values ( "
               "  :handle, "
               "  :id, "
               "  0 "
               ")";

    mGetFile = "select * "
               "  from files "
               " where (:handle is not null and handle = :handle) "
               "    or (:id is not null and id = :id)";
}

} // file_service
} // mega
