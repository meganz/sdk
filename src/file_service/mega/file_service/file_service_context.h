#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/client_forward.h>
#include <mega/common/database.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_context_badge_forward.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_context_badge_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_service_queries.h>
#include <mega/file_service/file_service_result_or_forward.h>
#include <mega/file_service/file_storage.h>
#include <mega/file_service/from_file_id_map.h>

namespace mega
{
namespace file_service
{

class FileServiceContext
{
    template<typename T>
    auto getFromIndex(FileID id, FromFileIDMap<std::weak_ptr<T>>& map) -> std::shared_ptr<T>;

    auto infoFromDatabase(FileID id, bool open) -> std::pair<FileInfoContextPtr, FileAccessPtr>;

    auto infoFromIndex(FileID id, bool open) -> std::pair<FileInfoContextPtr, FileAccessPtr>;

    auto info(FileID id, bool open) -> std::pair<FileInfoContextPtr, FileAccessPtr>;

    auto openFromCloud(FileID id) -> FileServiceResultOr<FileContextPtr>;

    auto openFromDatabase(FileID id) -> FileServiceResultOr<FileContextPtr>;

    auto openFromIndex(FileID id) -> FileContextPtr;

    template<typename T>
    auto removeFromIndex(FileID id, FromFileIDMap<T>& map) -> void;

    common::Client& mClient;
    FileStorage mStorage;
    common::Database mDatabase;
    FileServiceQueries mQueries;

    FromFileIDMap<FileContextWeakPtr> mFileContexts;
    FromFileIDMap<FileInfoContextWeakPtr> mInfoContexts;

    // This lock serializes access to the context's members.
    //
    // Note that if we want to run some query on the database, we must
    // explicitly lock mDatabase, too.
    common::SharedMutex mLock;
    common::ActivityMonitor mActivities;

public:
    FileServiceContext(common::Client& client);

    ~FileServiceContext();

    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    auto open(FileID id) -> FileServiceResultOr<File>;

    auto removeFromIndex(FileContextBadge badge, FileID id) -> void;

    auto removeFromIndex(FileInfoContextBadge badge, FileID id) -> void;
}; // FileServiceContext

} // file_service
} // mega
