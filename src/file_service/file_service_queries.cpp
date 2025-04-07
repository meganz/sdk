#include <mega/common/database.h>
#include <mega/file_service/file_service_queries.h>

namespace mega
{
namespace file_service
{

using namespace common;

static const std::string kName = "FileServiceQueries";

FileServiceQueries::FileServiceQueries(Database& database):
    DestructionLogger(kName),
    mGetFile(database.query()),
    mConstructionLogger(kName)
{
    mGetFile = "select * "
               "  from files "
               " where (:handle is not null and handle = :handle) "
               "    or (:id is not null and id = :id)";
}

} // file_service
} // mega
