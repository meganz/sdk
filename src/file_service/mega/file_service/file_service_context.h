#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/client_forward.h>
#include <mega/common/database.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/construction_logger.h>
#include <mega/file_service/destruction_logger.h>
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

class FileServiceContext: DestructionLogger
{
    auto infoFromDatabase(FileID id) -> FileInfoContextPtr;

    auto infoFromIndex(FileID id) -> FileInfoContextPtr;

    template<typename T>
    auto removeFromIndex(FileID id, FromFileIDMap<T>& map) -> void;

    common::Client& mClient;
    FileStorage mStorage;
    common::Database mDatabase;
    FileServiceQueries mQueries;

    FromFileIDMap<FileInfoContextWeakPtr> mInfoContexts;
    common::SharedMutex mLock;
    common::ActivityMonitor mActivities;

    ConstructionLogger mConstructionLogger;

public:
    FileServiceContext(common::Client& client);

    ~FileServiceContext();

    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    auto removeFromIndex(FileInfoContextBadge badge, FileID id) -> void;
}; // FileServiceContext

} // file_service
} // mega
