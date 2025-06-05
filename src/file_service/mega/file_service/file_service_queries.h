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
    common::Query mAddFileRange;
    common::Query mGetFile;
    common::Query mGetFileRanges;
    common::Query mGetFileReferences;
    common::Query mRemoveFile;
    common::Query mRemoveFileRanges;
    common::Query mSetFileModificationTime;
    common::Query mSetFileReferences;
}; // FileServiceQueries

} // file_service
} // mega
