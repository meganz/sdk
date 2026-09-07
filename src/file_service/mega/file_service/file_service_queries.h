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
    common::Query mAddFileDuration;
    common::Query mAddFileID;
    common::Query mAddFileKeyData;
    common::Query mAddFileRange;
    common::Query mGetFile;
    common::Query mGetFileDuration;
    common::Query mGetFileIDByHandle;
    common::Query mGetFileIDs;
    common::Query mGetFileKeyData;
    common::Query mGetFileRanges;
    common::Query mGetFreeFileID;
    common::Query mGetNextFileID;
    common::Query mGetModifiedFiles;
    common::Query mGetReclaimableFiles;
    common::Query mGetStorageInfo;
    common::Query mGetStorageUsed;
    common::Query mRemoveFile;
    common::Query mRemoveFileID;
    common::Query mRemoveFileIDs;
    common::Query mRemoveFileRanges;
    common::Query mRemoveFiles;
    common::Query mSetFileAccessTime;
    common::Query mSetFileHandle;
    common::Query mSetFileModificationTime;
    common::Query mSetFileRemoved;
    common::Query mSetFileSize;
    common::Query mSetFileSizeAndModificationTime;
    common::Query mSetNextFileID;
}; // FileServiceQueries

} // file_service
} // mega
