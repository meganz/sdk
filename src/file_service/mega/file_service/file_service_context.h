#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/client_forward.h>
#include <mega/common/database.h>
#include <mega/common/instance_logger.h>
#include <mega/common/node_event_observer.h>
#include <mega/common/node_key_data.h>
#include <mega/common/shared_mutex.h>
#include <mega/common/task_executor.h>
#include <mega/file_service/file_context_badge_forward.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_event_emitter.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_id_vector.h>
#include <mega/file_service/file_info_context_badge_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_set.h>
#include <mega/file_service/file_service_callbacks.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_service_forward.h>
#include <mega/file_service/file_service_options.h>
#include <mega/file_service/file_service_queries.h>
#include <mega/file_service/file_service_result_or_forward.h>
#include <mega/file_service/file_storage.h>
#include <mega/file_service/from_file_id_map.h>
#include <mega/file_service/storage_info.h>
#include <mega/scoped_helpers.h>

#include <atomic>
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

class FileServiceContext: common::NodeEventObserver
{
    // Processes client node events.
    class EventProcessor;

    // Returned from fileContextFrom(Cloud|Database|Index).
    using FileContextResult = FileServiceResultOr<FileContextPtr>;

    // Tracks state necessary for reclaim.
    class ReclaimContext;

    // Convenience.
    using ReclaimContextPtr = std::shared_ptr<ReclaimContext>;

    template<typename Lock>
    FileID allocateID(Lock&& lock, common::Transaction& transaction);

    // Remove unmodified files from the database and from disk.
    void cleanCache();

    template<typename Lock>
    void deallocateID(FileID id, Lock&& lock, common::Transaction& transaction);

    auto fileContextFromCloud(FileID id) -> FileContextResult;

    auto fileContextFromDatabase(FileID id) -> FileContextResult;

    template<typename Lock>
    auto fileContextFromIndex(FileID id, Lock&& lock) -> FileContextResult;

    template<typename Lock, typename T>
    auto getFromIndex(FileID id, Lock&& lock, FromFileIDMap<std::weak_ptr<T>>& map)
        -> std::shared_ptr<T>;

    auto infoContextFromDatabase(FileID id) -> FileInfoContextPtr;

    template<typename Lock>
    auto infoContextFromIndex(FileID id, Lock&& lock) -> FileInfoContextPtr;

    auto infoContext(FileID id) -> FileServiceResultOr<FileInfoContextPtr>;

    template<typename Transaction>
    auto keyData(FileID id, Transaction&& transaction) -> std::optional<common::NodeKeyData>;

    template<typename Transaction>
    auto ranges(FileID id, Transaction&& transaction) -> FileRangeSet;

    void reclaimTaskCallback(common::Activity& activity,
                             std::chrono::steady_clock::time_point when,
                             const common::Task& task);

    auto reclaimable(const ReclaimOptions& reclaimOptions) -> FileServiceResultOr<FileIDVector>;

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
    auto storageInfo(Lock&& lock,
                     const ReclaimOptions& reclaimOptions,
                     Transaction&& transaction) -> StorageInfo;

    template<typename Lock, typename Transaction>
    auto storageUsed(Lock&& lock, Transaction&& transaction) -> std::uint64_t;

    void updated(common::NodeEventQueue& events) override;

    // Logs instance lifetime.
    common::InstanceLogger<FileServiceContext> mInstanceLogger;

    common::Client& mClient;

    // Are we being destructed?
    std::atomic<bool> mDeinitialized;

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

    // Responsible for cleaning the service's cache on destruction.
    //
    // Note that mCacheCleaner makes use of the mDatabase, mQueries and
    // mStorage members directly above during destruction.
    ScopedDestructor mCacheCleaner;

    FromFileIDMap<FileContextWeakPtr> mFileContexts;
    std::condition_variable_any mInfoContextRemoved;
    FromFileIDMap<FileInfoContextWeakPtr> mInfoContexts;

    // This lock serializes access to the context's members.
    //
    // Note that if we want to run some query on the database, we must
    // explicitly lock mDatabase, too.
    common::SharedMutex mLock;

    // Tracks any reclaim in progress.
    ReclaimContextPtr mReclaimContext;

    // Serializes access to mReclaimContext.
    std::mutex mReclaimContextLock;

    // Tracks any scheduled reclamation.
    common::Task mReclaimTask;

    // Serializes access to mReclaimTask.
    std::recursive_mutex mReclaimTaskLock;

    // Responsible for event notification.
    FileEventEmitter mEventEmitter;

    // What service does this context belong to?
    FileService& mService;

    // Lets us execute tasks on a thread pool.
    common::TaskExecutor mExecutor;

    // This member will ensure the context isn't destroyed until any related
    // activities have been completed.
    //
    // Since each File(Info)?Context is passed an activity when they are
    // instantiated, this means that this member's destructor will wait
    // until all File(Info)?Contexts that refer to this context have been
    // destroyed before allowing this context itself to be destroyed.
    common::ActivityMonitor mContextMonitor;

    // This member will ensure that the context isn't destroyed until any
    // automatic reclamations it has scheduled have completed.
    common::ActivityMonitor mReclaimMonitor;

public:
    FileServiceContext(common::Client& client,
                       FileService& service,
                       const UserStoragePath& userStoragePath);

    ~FileServiceContext();

    // Add a foreign file to the service.
    auto add(NodeHandle handle,
             const common::NodeKeyData& keyData,
             std::uint64_t size) -> FileServiceResultOr<FileID>;

    // Notify observer when a file changes.
    FileEventObserverID addObserver(FileEventObserver observer);

    // Notify observer when a specific file changes.
    FileEventObserverID addObserver(FileID id, FileEventObserver observer);

    // Let the service know it should clean the cache on destruction.
    void cleanCacheOnDestruction();

    // Retrieve a reference to this service's client.
    common::Client& client();

    // Create a new file.
    auto create(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>;

    // Retrieve a reference to this service's database.
    common::Database& database();

    // Where is the service storing this user's database?
    LocalPath databasePath() const;

    // Is this service being destroyed?
    bool deinitializing() const;

    // Get a reference to this context's task executor.
    common::TaskExecutor& executor();

    // Retrieve information about a file managed by this service.
    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    // Emit a file event.
    void notify(const FileEvent& event);

    // Open a file for reading or writing.
    auto open(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>;
    auto open(FileID id) -> FileServiceResultOr<File>;

    // Find out where the service is storing the specified file.
    LocalPath path(FileID id) const;

    // Return a reference to this service's queries.
    FileServiceQueries& queries();

    // Purge all files from storage.
    auto purge() -> FileServiceResult;

    // Reclaim storage space.
    void reclaim(ReclaimCallback callback, const ReclaimOptions& reclaimOptions);

    // Remove an observer.
    void removeObserver(FileEventObserverID id);

    // Remove an observer from a specific file.
    void removeObserver(FileID id, FileEventObserverID observerID);

    // Retrieve the file service's current reclaim options.
    ReclaimOptions reclaimOptions();

    // Let the context know its reclamation options has changed.
    void reclaimOptionsChanged(const ReclaimOptions& newOptions);

    // Remove a file context from our index.
    void removeFromIndex(FileContextBadge badge, FileID id);

    // Remove a file info context from our index.
    void removeFromIndex(FileInfoContextBadge badge, FileInfoContext& context);

    // Retrieve the service's current options.
    ServiceOptions serviceOptions();

    // How much storage space is the service using?
    // Get storage size information in detail such as reclaimable storage size
    auto storageInfo(const ReclaimOptions* options) -> FileServiceResultOr<StorageInfo>;

    // How much storage space is the service using? Better performance than storageInfo but with
    // less information
    auto storageUsed() -> FileServiceResultOr<std::uint64_t>;

    // Find out where the service is storing a particular file.
    LocalPath userFilePath(FileID id) const;
}; // FileServiceContext

} // file_service
} // mega
