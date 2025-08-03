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
    common::Query mAddFileID;
    common::Query mAddFileRange;
    common::Query mGetFile;
    common::Query mGetFileByNameAndParentHandle;
    common::Query mGetFileIDs;
    common::Query mGetFileRanges;
    common::Query mGetFileReferences;
    common::Query mGetFreeFileID;
    common::Query mGetNextFileID;
    common::Query mGetReclaimableFiles;
    common::Query mGetStorageUsed;
    common::Query mRemoveFile;
    common::Query mRemoveFileID;
    common::Query mRemoveFileRanges;
    common::Query mSetFileAccessTime;
    common::Query mSetFileHandle;
    common::Query mSetFileLocation;
    common::Query mSetFileModificationTime;
    common::Query mSetFileReferences;
    common::Query mSetFileRemoved;
    common::Query mSetFileSize;
    common::Query mSetNextFileID;
}; // FileServiceQueries

} // file_service
} // mega
