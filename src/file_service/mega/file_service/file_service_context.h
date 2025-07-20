#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/client_forward.h>
#include <mega/common/database.h>
#include <mega/common/shared_mutex.h>
#include <mega/common/task_executor.h>
#include <mega/common/task_queue_forward.h>
#include <mega/file_service/file_context_badge_forward.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_context_badge_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_service_options.h>
#include <mega/file_service/file_service_queries.h>
#include <mega/file_service/file_service_result_or_forward.h>
#include <mega/file_service/file_storage.h>
#include <mega/file_service/from_file_id_map.h>

#include <optional>
#include <vector>

namespace mega
{

class LocalPath;

namespace file_service
{

class FileServiceContext
{
    template<typename Lock>
    FileID allocateID(Lock&& lock, common::Transaction& transaction);

    template<typename Lock>
    void deallocateID(FileID id, Lock&& lock, common::Transaction& transaction);

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

    template<typename Lock>
    auto rangesFromDatabase(FileID id, Lock&& lock) -> std::optional<FileRangeVector>;

    template<typename Lock>
    auto rangesFromIndex(FileID id, Lock&& lock) -> std::optional<FileRangeVector>;

    auto reclaimable(const FileServiceOptions& options) -> std::vector<FileID>;

    template<typename Lock, typename T>
    bool removeFromIndex(FileID id, Lock&& lock, FromFileIDMap<T>& map);

    template<typename T>
    bool removeFromIndex(FileID id, FromFileIDMap<T>& map);

    template<typename Lock, typename Transaction>
    auto storageUsed(Lock&& lock, Transaction&& transaction) -> std::uint64_t;

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

    // Specifies various metrics that control how the service behaves.
    FileServiceOptions mOptions;

    // Serializes access to mOptions.
    common::SharedMutex mOptionsLock;

    // This member will ensure the context isn't destroyed until any related
    // activities have been completed.
    //
    // Since each File(Info)?Context is passed an activity when they are
    // instantiated, this means that this member's destructor will wait
    // until all File(Info)?Contexts that refer to this context have been
    // destroyed before allowing this context itself to be destroyed.
    common::ActivityMonitor mActivities;

    // Lets us execute tasks on a thread pool.
    common::TaskExecutor mExecutor;

public:
    FileServiceContext(common::Client& client, const FileServiceOptions& options);

    ~FileServiceContext();

    // Retrieve a reference to this service's client.
    common::Client& client();

    // Create a new file.
    auto create(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>;

    // Retrieve a reference to this service's database.
    common::Database& database();

    // Execute a task on this service's thread pool.
    auto execute(std::function<void(const common::Task&)> function) -> common::Task;

    // Retrieve information about a file managed by this service.
    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    // Open a file for reading or writing.
    auto open(FileID id) -> FileServiceResultOr<File>;

    // Update the file service's options.
    void options(const FileServiceOptions& options);

    // Retrieve the file service's current options.
    FileServiceOptions options();

    // Find out where the service is storing the specified file.
    LocalPath path(FileID id) const;

    // Return a reference to this service's queries.
    FileServiceQueries& queries();

    // Determine what ranges of a file are currently in storage.
    auto ranges(FileID id) -> FileServiceResultOr<FileRangeVector>;

    // Remove a file context from our index.
    void removeFromIndex(FileContextBadge badge, FileID id);

    // Remove a file info context from our index.
    void removeFromIndex(FileInfoContextBadge badge, FileInfoContext& context);

    // How much storage space is the service using?
    auto storageUsed() -> FileServiceResultOr<std::uint64_t>;
}; // FileServiceContext

} // file_service
} // mega
