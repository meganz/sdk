#pragma once

#include <mega/common/database_forward.h>
#include <mega/common/query.h>

namespace mega
{
namespace file_service
{

struct FileServiceQueries
{
    explicit FileServiceQueries(common::Database& database);

    common::Query mAddFile;
    common::Query mGetFile;
}; // FileServiceQueries

} // file_service
} // mega
