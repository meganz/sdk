#pragma once

#include <mega/common/database_forward.h>
#include <mega/common/query.h>
#include <mega/file_service/construction_logger.h>
#include <mega/file_service/destruction_logger.h>

namespace mega
{
namespace file_service
{

struct FileServiceQueries: DestructionLogger
{
    explicit FileServiceQueries(common::Database& database);

    common::Query mGetFile;
    ConstructionLogger mConstructionLogger;
}; // FileServiceQueries

} // file_service
} // mega
