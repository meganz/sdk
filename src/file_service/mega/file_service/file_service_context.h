#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/client_forward.h>
#include <mega/common/database.h>
#include <mega/common/node_event_observer.h>
#include <mega/common/shared_mutex.h>
#include <mega/common/task_executor.h>
#include <mega/common/task_queue.h>
#include <mega/file_service/file_context_badge_forward.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_event_emitter.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_id_vector.h>
#include <mega/file_service/file_info_context_badge_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_service_callbacks.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_service_options.h>
#include <mega/file_service/file_service_queries.h>
#include <mega/file_service/file_service_result_or_forward.h>
#include <mega/file_service/file_storage.h>
#include <mega/file_service/from_file_id_map.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <optional>
#include <vector>

namespace mega
{

class LocalPath;

namespace file_service
{

class FileServiceContext: common::NodeEventObserver, public FileEventEmitter
{
    // Processes client node events.
    class EventProcessor;

    // Returned from info(From(Database|Index)).
    using InfoContextResult = FileServiceResultOr<std::pair<FileInfoContextPtr, FileAccessPtr>>;

    // Returned from openFrom(Cloud|Database|Index).
    using FileContextResult = FileServiceResultOr<FileContextPtr>;

    // Tracks state necessary for reclaim.
    class ReclaimContext;

    // Convenience.
    using ReclaimContextPtr = std::shared_ptr<ReclaimContext>;

    template<typename Lock>
    FileID allocateID(Lock&& lock, common::Transaction& transaction);

    template<typename Lock>
    void deallocateID(FileID id, Lock&& lock, common::Transaction& transaction);

    template<typename Lock, typename T>
    auto getFromIndex(FileID id, Lock&& lock, FromFileIDMap<std::weak_ptr<T>>& map)
        -> std::shared_ptr<T>;

    auto infoFromDatabase(FileID id, bool open) -> InfoContextResult;

    template<typename Lock>
    auto infoFromIndex(FileID id, Lock&& lock, bool open) -> InfoContextResult;

    auto info(FileID id, bool open) -> InfoContextResult;

    auto openFromCloud(FileID id) -> FileContextResult;

    auto openFromDatabase(FileID id) -> FileContextResult;

    template<typename Lock>
    auto openFromIndex(FileID id, Lock&& lock) -> FileContextResult;

    template<typename Transaction>
    auto ranges(FileID id, Transaction&& transaction) -> FileRangeVector;

    void reclaimTaskCallback(common::Activity& activity,
                             std::chrono::steady_clock::time_point when,
                             const common::Task& task);

    auto reclaimable() -> FileServiceResultOr<FileIDVector>;

    template<typename ContextLock, typename DatabaseLock>
    void remove(ContextLock&& contextLock,
                DatabaseLock&& databaseLock,
                FileID id,
                common::Transaction& transaction);

    template<typename Lock>
    void removeFromDatabase(FileID id, Lock&& lock, common::Transaction& transaction);

    template<typename Lock, typename T>
    bool removeFromIndex(FileID id, Lock&& lock, FromFileIDMap<T>& map);

    template<typename T>
    bool removeFromIndex(FileID id, FromFileIDMap<T>& map);

    void purgeRemovedFiles();

    template<typename Lock, typename Transaction>
    auto storageUsed(Lock&& lock, Transaction&& transaction) -> std::uint64_t;

    void updated(common::NodeEventQueue& events) override;

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
    std::condition_variable_any mInfoContextRemoved;
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

    // Tracks any reclaim in progress.
    ReclaimContextPtr mReclaimContext;

    // Serializes access to mReclaimContext.
    std::mutex mReclaimContextLock;

    // Tracks any scheduled reclamation.
    common::Task mReclaimTask;

    // Serializes access to mReclaimTask.
    std::recursive_mutex mReclaimTaskLock;

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
    auto open(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>;
    auto open(FileID id) -> FileServiceResultOr<File>;

    // Update the file service's options.
    void options(const FileServiceOptions& options);

    // Retrieve the file service's current options.
    FileServiceOptions options();

    // Find out where the service is storing the specified file.
    LocalPath path(FileID id) const;

    // Return a reference to this service's queries.
    FileServiceQueries& queries();

    // Purge all files from storage.
    auto purge() -> FileServiceResult;

    // Reclaim storage space.
    void reclaim(ReclaimCallback callback);

    // Remove a file context from our index.
    void removeFromIndex(FileContextBadge badge, FileID id);

    // Remove a file info context from our index.
    void removeFromIndex(FileInfoContextBadge badge, FileInfoContext& context);

    // How much storage space is the service using?
    auto storageUsed() -> FileServiceResultOr<std::uint64_t>;
}; // FileServiceContext

} // file_service
} // mega
