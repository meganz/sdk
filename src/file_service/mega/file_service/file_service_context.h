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
    template<typename Lock, typename T>
    auto getFromIndex(FileID id, Lock&& lock, FromFileIDMap<std::weak_ptr<T>>& map)
        -> std::shared_ptr<T>;

    auto infoFromDatabase(FileID id, bool open) -> std::pair<FileInfoContextPtr, FileAccessPtr>;

    template<typename Lock>
    auto infoFromIndex(FileID id, Lock&& lock, bool open)
        -> std::pair<FileInfoContextPtr, FileAccessPtr>;

    auto info(FileID id, bool open) -> std::pair<FileInfoContextPtr, FileAccessPtr>;

    auto openFromCloud(FileID id) -> FileServiceResultOr<FileContextPtr>;

    auto openFromDatabase(FileID id) -> FileServiceResultOr<FileContextPtr>;

    template<typename Lock>
    auto openFromIndex(FileID id, Lock&& lock) -> FileContextPtr;

    template<typename T>
    auto removeFromIndex(FileID id, FromFileIDMap<T>& map) -> void;

    common::Client& mClient;

    // No locks are needed in order to make use of this member.
    //
    // As far as invariants are concerned, the member is sane as soon as it
    // completes its initialization.
    //
    // As for different threads making concurrent calls, that should also be
    // safe although we will be relying on the operating system itself to
    // synchronize calls to the filesystem.
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

    // This member should always come last as it will ensure the context
    // isn't destroyed until any related activities have been completed.
    //
    // Since each File(Info)?Context is passed an activity when they are
    // instantiated, this means that this member's destructor will wait
    // until all File(Info)?Contexts that refer to this context have been
    // destroyed before allowing this context itself to be destroyed.
    common::ActivityMonitor mActivities;

public:
    FileServiceContext(common::Client& client);

    ~FileServiceContext();

    // Retrieve a reference to this service's client.
    common::Client& client();

    // Retrieve information about a file managed by this service.
    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    // Open a file for reading or writing.
    auto open(FileID id) -> FileServiceResultOr<File>;

    // Remove a file context from our index.
    auto removeFromIndex(FileContextBadge badge, FileID id) -> void;

    // Remove a file info context from our index.
    auto removeFromIndex(FileInfoContextBadge badge, FileID id) -> void;
}; // FileServiceContext

} // file_service
} // mega
