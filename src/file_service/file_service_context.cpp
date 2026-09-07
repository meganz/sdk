#include <mega/common/client.h>
#include <mega/common/lock.h>
#include <mega/common/node_event.h>
#include <mega/common/node_event_queue.h>
#include <mega/common/node_event_type.h>
#include <mega/common/node_info.h>
#include <mega/common/scoped_query.h>
#include <mega/common/transaction.h>
#include <mega/common/utility.h>
#include <mega/file_service/database_builder.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_event.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_info_context_badge.h>
#include <mega/file_service/file_location.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_remove_event.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/logging.h>
#include <mega/filesystem.h>
#include <mega/mediafileattribute.h>
#include <mega/scoped_helpers.h>

#include <chrono>
#include <stdexcept>

namespace mega
{
namespace file_service
{

// Convenience.
using namespace common;
using namespace std::chrono;

class FileServiceContext::EventProcessor
{
    // Called to dispatch an event.
    void dispatch(const NodeEvent& event);

    // Retrieve a file's info context.
    FileInfoContextPtr info(FileID id);

    // Mark an in-memory file as removed.
    //
    // Returns true iff the file was in memory.
    bool mark(FileID id);

    // Remove a file from the database and from storage.
    void remove(FileID id);

    // Called when a node has been removed.
    //
    // If event describes a directory, delegate to removedDirectory(...).
    //
    // If event describes a file we manage, remove it.
    // Otherwise, ignore the event.
    void removed(const NodeEvent& event);

    // The service we're processing events for.
    FileServiceContext& mService;

    // Ensures we have exclusive access to the service.
    UniqueLock<SharedMutex> mServiceLock;

    // Ensures we have exclusive access to the database.
    UniqueLock<Database> mDatabaseLock;

    // Provides convenient access to the service's queries.
    FileServiceQueries& mQueries;

    // Provides access to the database while we process events.
    Transaction mTransaction;

public:
    EventProcessor(FileServiceContext& service);

    // Process zero or more node events.
    void operator()(NodeEventQueue& events);
}; // EventProcessor

class FileServiceContext::ReclaimContext
{
    // Reclaim a single file.
    void reclaim(ReclaimContextPtr context, FileID id);

    // Reclaim zero or more files in a batch.
    template<typename Lock>
    void reclaimBatch(ReclaimContextPtr context, Lock&& lock);

    // Called when a file has been reclaimed.
    void reclaimed(ReclaimContextPtr context, FileResultOr<std::uint64_t> result);

    // Logs instance lifetime.
    common::InstanceLogger<ReclaimContext> mInstanceLogger;

    // Make sure our service stays alive as long as we do.
    Activity mActivity;

    // Who should we call when reclamation completes?
    std::vector<ReclaimCallback> mCallbacks;

    // What files are we reclaiming?
    FileIDVector mIDs;

    // Serializes access to mCount, mReclaimed and mResult.
    std::recursive_mutex mLock;

    // Tracks how many files are currently being reclaimed.
    std::size_t mNumPending;

    // Tracks how much space we've recovered.
    std::uint64_t mReclaimed;

    // Tracks whether we encountered any failures.
    FileServiceResult mResult;

    // What service are we reclaiming storage for?
    FileServiceContext& mService;

public:
    ReclaimContext(FileServiceContext& service);

    // Called when the reclamation has completed.
    void completed(FileServiceResultOr<std::uint64_t> result);

    // Queue a callback for later execution.
    void queue(ReclaimCallback callback);

    // Reclaim zero or more files.
    void reclaim(ReclaimContextPtr context, const ReclaimOptions& reclaimOptions);
}; // ReclaimContext

static Database createDatabase(const LocalPath& databasePath);

static std::optional<std::uint32_t> extractDuration(const std::string& attributes,
                                                    const NodeKeyData& keyData);

static bool reclamationEnabled(const ReclaimOptions& options);

template<typename Lock>
FileID FileServiceContext::allocateID([[maybe_unused]] Lock&& lock, Transaction& transaction)
{
    assert(lock.mutex() == &mDatabase);
    assert(lock.owns_lock());

    // Check if we need to generate a new file ID.
    auto query = transaction.query(mQueries.mGetFreeFileID);

    query.execute();

    // We need to generate a new file ID.
    if (!query)
    {
        // Determine the next allocable ID.
        query = transaction.query(mQueries.mGetNextFileID);

        query.execute();

        // Latch the next available ID.
        auto next = query.field("next").get<std::uint64_t>();

        // Make sure we haven't exhausted the space of synthetic IDs.
        assert(!synthetic(next));

        if (synthetic(next))
            throw std::runtime_error("Exhausted space of synthetic IDs");

        // Note that this ID has been allocated.
        query = transaction.query(mQueries.mSetNextFileID);

        query.param(":next").set(next + 1);
        query.execute();

        // Return the ID to our caller.
        return FileID::from(next);
    }

    // We can recycle a previously allocated ID.
    auto id = query.field("id").get<std::uint64_t>();

    // id is no longer available for allocation.
    query = transaction.query(mQueries.mRemoveFileID);

    query.param(":id").set(id);
    query.execute();

    // Return the ID to our caller.
    return FileID::from(id);
}

void FileServiceContext::cleanCache()
try
{
    FSDebug1("Cleaning cache...");

    // No lock should be necessary as we're called during destruction.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFileIDs);

    query.param(":dirty").set(false);

    // Remove each unmodified file from the disk.
    for (query.execute(); query; ++query)
        mStorage.removeFile(query.field("id").get<FileID>(), std::nothrow);

    // Remove unmodified files from the database.
    query = transaction.query(mQueries.mRemoveFiles);

    query.param(":dirty").set(false);
    query.execute();

    // Persist database changes.
    transaction.commit();

    FSDebug1("Cache cleaned");
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to clean cache: %s", exception.what());
}

template<typename Lock>
void FileServiceContext::deallocateID(FileID id,
                                      [[maybe_unused]] Lock&& lock,
                                      Transaction& transaction)
{
    assert(synthetic(id));
    assert(lock.mutex() == &mDatabase);
    assert(lock.owns_lock());

    // Add this id to our free list.
    auto query = transaction.query(mQueries.mAddFileID);

    query.param(":id").set(id);
    query.execute();
}

auto FileServiceContext::fileContextFromCloud(FileID id) -> FileServiceResultOr<FileContextPtr>
{
    // Synthetic IDs are never a valid node handle.
    if (synthetic(id))
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    // Check if a node exists in the cloud with this ID.
    auto node = mClient.get(id.toHandle());

    // Couldn't get a reference to the node.
    if (!node)
    {
        // Because it doesn't exist in the cloud.
        if (node.error() == API_ENOENT)
            return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

        // Because we hit some unexpected error.
        return unexpected(FILE_SERVICE_UNEXPECTED);
    }

    // You can't open a directory as a file.
    if (node->mIsDirectory)
        return unexpected(FILE_SERVICE_FILE_IS_A_DIRECTORY);

    // Try and retrieve the node's key data.
    auto keyData = mClient.keyData(node->mHandle, false);

    // Couldn't get our hands on the nodes's key data.
    if (!keyData)
        return unexpected(FILE_SERVICE_UNEXPECTED);

    // Try and retrieve the node's file attributes.
    auto attributes = mClient.fileAttributes(node->mHandle);

    // Couldn't get our hands on the node's file attributes.
    if (!attributes)
        return unexpected(FILE_SERVICE_UNEXPECTED);

    // Try and extract the node's duration.
    auto duration = extractDuration(*attributes, *keyData);

    // Make sure no one's changing our indexes.
    UniqueLock lockContexts(mLock);

    // Make sure no one's changing the database.
    UniqueLock lockDatabase(mDatabase);

    // Check if another thread's opened (or removed) this file.
    auto maybeFile = fileContextFromIndex(id, lockContexts);

    // Another thread opened (or removed) this file.
    if (!maybeFile || *maybeFile)
        return maybeFile;

    // Compute the file's access time.
    auto accessed = now();

    // Latch the file's size.
    auto size = static_cast<std::uint64_t>(node->mSize);

    // Add the file to the database.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mAddFile);

    query.param(":accessed").set(accessed);
    query.param(":allocated_size").set(0u);
    query.param(":dirty").set(false);
    query.param(":handle").set(node->mHandle);
    query.param(":id").set(id);
    query.param(":modified").set(node->mModified);
    query.param(":removed").set(false);
    query.param(":reported_size").set(0u);
    query.param(":size").set(size);

    query.execute();

    // Add the file's duration to the database.
    if (duration && *duration)
    {
        query = transaction.query(mQueries.mAddFileDuration);

        query.param(":duration").set(*duration);
        query.param(":id").set(id);

        query.execute();
    }

    // Add the file to storage.
    auto file = mStorage.addFile(id);

    // Persist our database changes.
    transaction.commit();

    // Clarity.
    auto allocatedSize = 0u;
    auto dirty = false;
    auto reportedSize = 0u;

    // Create a context to represent this file's information.
    auto info = std::make_shared<FileInfoContext>(accessed,
                                                  mContextMonitor.begin(),
                                                  allocatedSize,
                                                  dirty,
                                                  duration,
                                                  node->mHandle,
                                                  id,
                                                  node->mModified,
                                                  reportedSize,
                                                  *this,
                                                  size);

    // Make sure this file's info is in our index.
    mInfoContexts.emplace(id, info);

    // Create a context to represent the file itself.
    auto context = std::make_shared<FileContext>(mContextMonitor.begin(),
                                                 std::move(file),
                                                 std::move(info),
                                                 std::nullopt,
                                                 FileRangeSet(),
                                                 *this);

    // Make sure the file is in our index.
    mFileContexts.emplace(id, context);

    // Return the file to our caller.
    return context;
}

auto FileServiceContext::fileContextFromDatabase(FileID id) -> FileServiceResultOr<FileContextPtr>
{
    // Try and get our hands on the file's information.
    auto maybeInfo = infoContext(id);

    // File's been removed.
    if (!maybeInfo)
        return unexpected(maybeInfo.error());

    // Clarity.
    auto& info = *maybeInfo;

    // File isn't in storage so open it from the cloud.
    if (!info)
        return fileContextFromCloud(id);

    // Acquire lock.
    UniqueLock lock(mLock);

    // File's been removed.
    if (info->removed())
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    // Check if another thread opened the file.
    auto maybeFile = fileContextFromIndex(info->id(), lock);

    // File was opened (or removed) by another thread.
    if (!maybeFile || *maybeFile)
        return maybeFile;

    // Make sure we're using the file's canonical ID.
    id = info->id();

    // Retrieve this file's key data and ranges from the database.
    std::optional<NodeKeyData> keyData;
    FileRangeSet ranges;

    {
        // Acquire database lock.
        UniqueLock lockDatabase(mDatabase);

        // So we can safely access the database.
        auto transaction = mDatabase.transaction();

        // Retrieve the file's key data from the database.
        keyData = this->keyData(id, transaction);

        // Retrieve the file's ranges from the database.
        ranges = this->ranges(id, transaction);
    }

    // Instantiate a new file context.
    auto context = std::make_shared<FileContext>(mContextMonitor.begin(),
                                                 mStorage.getFile(id),
                                                 std::move(info),
                                                 std::move(keyData),
                                                 ranges,
                                                 *this);

    // Add the context to our index.
    mFileContexts.emplace(id, context);

    // Return the context to our caller.
    return context;
}

template<typename Lock>
auto FileServiceContext::fileContextFromIndex(FileID id, Lock&& lock)
    -> FileServiceResultOr<FileContextPtr>
{
    // Check if the file's in memory.
    auto file = getFromIndex(id, std::forward<Lock>(lock), mFileContexts);

    // File isn't in memory.
    if (!file)
        return nullptr;

    // File's been marked as removed.
    if (file->removed())
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    // Return file to caller.
    return file;
}

template<typename Lock, typename T>
auto FileServiceContext::getFromIndex(FileID id,
                                      [[maybe_unused]] Lock&& lock,
                                      FromFileIDMap<std::weak_ptr<T>>& map) -> std::shared_ptr<T>
{
    assert(lock.mutex() == &mLock);
    assert(lock.owns_lock());

    // Try and find the entry in the map.
    auto entry = map.find(id);

    // No entry in the map.
    if (entry == map.end())
        return nullptr;

    // Try and get a strong reference to the entry's instance.
    auto instance = entry->second.lock();

    // Entry references a dead instance.
    if (!instance)
        return map.erase(entry), nullptr;

    // Return instance to caller.
    return instance;
}

auto FileServiceContext::infoContextFromDatabase(FileID id) -> FileInfoContextPtr
{
    // Make sure no one is changing our indexes.
    UniqueLock lockContexts(mLock);

    // Make sure no one is changing our database.
    UniqueLock lockDatabase(mDatabase);

    // Check if another thread loaded this file's info.
    auto info = infoContextFromIndex(id, lockContexts);

    // Info was loaded by another thread.
    if (info)
        return info;

    // Check if this file exists in the database.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFile);

    query.param(":handle").set(nullptr);
    query.param(":id").set(id);
    query.param(":removed").set(false);

    // The caller's looking up the file by a node handle.
    if (!synthetic(id))
        query.param(":handle").set(id.toHandle());

    query.execute();

    // We know nothing about this file.
    if (!query)
        return nullptr;

    // Latch the file's attributes from the database.
    auto accessed = query.field("accessed").get<std::int64_t>();
    auto allocatedSize = query.field("allocated_size").get<std::uint64_t>();
    auto dirty = query.field("dirty").get<bool>();
    auto handle = NodeHandle();
    auto modified = query.field("modified").get<std::int64_t>();
    auto reportedSize = query.field("reported_size").get<std::uint64_t>();
    auto size = query.field("size").get<std::uint64_t>();

    if (!query.field("handle").null())
        handle = query.field("handle").get<NodeHandle>();

    id = query.field("id").get<FileID>();

    // Load the file's duration, if any.
    std::optional<std::uint32_t> duration;

    query = transaction.query(mQueries.mGetFileDuration);

    query.param(":id").set(id);

    // File has a duration.
    if (query.execute())
        duration = query.field("duration").get<std::uint32_t>();

    // Instantiate a context to represent this file's information.
    info = std::make_shared<FileInfoContext>(accessed,
                                             mContextMonitor.begin(),
                                             allocatedSize,
                                             dirty,
                                             duration,
                                             handle,
                                             id,
                                             modified,
                                             reportedSize,
                                             *this,
                                             size);

    // Add the context to our index.
    mInfoContexts.emplace(id, info);

    // Return the file's information to our caller.
    return info;
}

template<typename Lock>
auto FileServiceContext::infoContextFromIndex(FileID id, Lock&& lock) -> FileInfoContextPtr
{
    // Check if this file's information is in the index.
    return getFromIndex(id, std::forward<Lock>(lock), mInfoContexts);
}

auto FileServiceContext::infoContext(FileID id) -> FileServiceResultOr<FileInfoContextPtr>
{
    // Check if the file's in memory.
    auto info = infoContextFromIndex(id, SharedLock(mLock));

    // File's not in memory.
    if (!info)
        return infoContextFromDatabase(id);

    // File's been marked as removed.
    if (info->removed())
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    // Return the file's information to our caller.
    return info;
}

template<typename Transaction>
auto FileServiceContext::keyData(FileID id, Transaction&& transaction) -> std::optional<NodeKeyData>
{
    assert(transaction.inProgress());

    auto query = transaction.query(mQueries.mGetFileKeyData);

    query.param(":id").set(id);

    if (!query.execute())
        return std::nullopt;

    using MaybeString = std::optional<std::string>;

    NodeKeyData keyData;

    keyData.mChatAuth = query.field("chat_auth").template get<MaybeString>();
    keyData.mIsPublicHandle = query.field("is_public").template get<bool>();
    keyData.mKeyAndIV = query.field("key_and_iv").template get<std::string>();
    keyData.mPrivateAuth = query.field("private_auth").template get<MaybeString>();
    keyData.mPublicAuth = query.field("public_auth").template get<MaybeString>();

    // Sanity.
    assert(keyData.mKeyAndIV.size() == FILENODEKEYLENGTH);

    return keyData;
}

template<typename Transaction>
auto FileServiceContext::ranges(FileID id, Transaction&& transaction) -> FileRangeSet
{
    assert(transaction.inProgress());

    auto query = transaction.query(mQueries.mGetFileRanges);
    auto ranges = FileRangeSet();

    query.param(":id").set(id);

    for (query.execute(); query; ++query)
    {
        auto begin = query.field("begin").template get<std::uint64_t>();
        auto end = query.field("end").template get<std::uint64_t>();

        ranges.add(FileRange(begin, end));
    }

    return ranges;
}

void FileServiceContext::reclaimTaskCallback(Activity& activity,
                                             steady_clock::time_point when,
                                             const Task& task)
{
    FSInfo1("reclaim task callback");

    // Exchange mReclaimTask with task.
    auto exchange = [oldTask = task, this](Task task)
    {
        // Acquire task lock.
        std::lock_guard guard(mReclaimTaskLock);

        // We've been superceded by another task.
        if (oldTask != mReclaimTask)
            return;

        // Update the context's reclamation task.
        mReclaimTask = std::move(task);
    }; // exchange

    // Client's shutting down or reclamation has been disabled.
    if (task.aborted())
    {
        FSInfo1("reclaim task is aborted");
        return exchange(Task());
    }

    // Schedule another reclamation in the future.
    auto reschedule = [activity = std::move(activity), exchange, this]()
    {
        // Get the service's current options.
        const auto reclaimOptions = this->reclaimOptions();

        // Reclamation has been disabled.
        if (!reclamationEnabled(reclaimOptions))
        {
            FSInfo1("reclaim task is disabled");
            return exchange(Task());
        }

        // When should the next reclamation occur?
        const auto when = steady_clock::now() + reclaimOptions.mPeriod;

        FSInfoF("reclaim task is scheduled in %d seconds", reclaimOptions.mPeriod.count());

        // Keep exchange below simple.
        auto callback = std::bind(&FileServiceContext::reclaimTaskCallback,
                                  this,
                                  std::move(activity),
                                  when,
                                  std::placeholders::_1);

        // Schedule another reclamation for the future.
        exchange(mExecutor.execute(std::move(callback), when, false));
    }; // reschedule

    // No reclamation needed at this time.
    if (steady_clock::now() < when)
        return reschedule();

    // Called when reclamation has completed.
    auto reclaimed = [reschedule = std::move(reschedule)](auto)
    {
        // Schedule another reclamation in the future.
        reschedule();
    }; // reclaimed

    // Try and reclaim storage.
    reclaim(std::move(reclaimed), reclaimOptions());
}

auto FileServiceContext::reclaimable(const ReclaimOptions& reclaimOptions)
    -> FileServiceResultOr<FileIDVector>
try
{
    // Convenience.
    const auto reclaimThreshold = reclaimOptions.mReclaimThreshold;
    const auto reclaimTarget = reclaimOptions.mReclaimTarget;

    // No quota? No need to reclaim anything.
    if (!reclaimThreshold)
        return FileIDVector();

    // So we have exclusive access to the database.
    UniqueLock lock(mDatabase);

    // So we can safely access the database.
    auto transaction = mDatabase.transaction();

    // Figure out how much storage we're currently using.
    auto used = storageUsed(lock, transaction);

    // No need to reclaim any storage.
    if (reclaimThreshold.value() >= used)
        return FileIDVector();

    // Get the allocated size and ID of all files in storage.
    auto query = transaction.query(mQueries.mGetReclaimableFiles);

    // Retrieve access time threshold.
    auto threshold = reclaimOptions.mAgeThreshold;

    // Compute maximum reclaimable access time.
    auto accessed = system_clock::now() - threshold;

    // Specify maximum reclaimable access time.
    query.param(":accessed").set(system_clock::to_time_t(accessed));

    // Tracks the IDs of the files we can reclaim.
    FileIDVector ids;

    // Collect as many IDs for reclamation as necessary until we are no longer above our target
    // storage usage. Condition should be aligned with the one in mGetStorageInfo.
    for (query.execute(); query && used > reclaimTarget; ++query)
    {
        // Latch the file's ID and allocated size.
        auto id = query.field("id").get<FileID>();
        auto size = query.field("allocated_size").get<std::uint64_t>();

        // Add the ID to our vector.
        ids.emplace_back(id);

        // Decrease amount of used storage.
        used -= std::min(size, used);
    }

    // Return vector of reclaimable IDs to our caller.
    return ids;
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to determine which files can be reclaimed: %s", exception.what());

    return FILE_SERVICE_UNEXPECTED;
}

// No lock necessary as we're called directly from the constructor.
void FileServiceContext::purgeRemovedFiles()
try
{
    // Tracks synthetic file IDs that we need to deallocate.
    FileIDVector ids;

    // So we can safely access the database.
    auto transaction = mDatabase.transaction();

    // Retrieve the ID of each file marked for removal.
    auto query = transaction.query(mQueries.mGetFileIDs);

    query.param(":removed").set(true);

    // Iterate over each file, purging its data from storage.
    for (query.execute(); query; ++query)
    {
        // Latch the file's ID.
        auto id = query.field("id").get<FileID>();

        // File's ID is synthetic and needs to be deallocated.
        if (synthetic(id))
            ids.emplace_back(id);

        // Remove the file's data from storage.
        mStorage.removeFile(id);
    }

    // Deallocate synthetic file IDs.
    query = transaction.query(mQueries.mAddFileID);

    for (auto id: ids)
    {
        query.param(":id").set(id);
        query.execute();
    }

    // Removed removed files from the database.
    query = transaction.query(mQueries.mRemoveFiles);

    query.param(":removed").set(true);
    query.execute();

    // Persist database changes.
    transaction.commit();
}

catch (std::runtime_error& exception)
{
    // Leave a trail so we know something went wrong.
    FSErrorF("Unable to purge removed files: %s", exception.what());
}

template<typename ContextLock, typename DatabaseLock>
void FileServiceContext::remove([[maybe_unused]] ContextLock&& contextLock,
                                DatabaseLock&& databaseLock,
                                FileID id,
                                common::Transaction& transaction)
{
    // Sanity.
    assert(contextLock.mutex() == &mLock);
    assert(contextLock.owns_lock());
    assert(databaseLock.mutex() == &mDatabase);
    assert(databaseLock.owns_lock());

    // Remove the file from the database.
    removeFromDatabase(id, databaseLock, transaction);

    // Remove the file from storage.
    mStorage.removeFile(id);
}

template<typename Lock>
void FileServiceContext::removeFromDatabase(FileID id,
                                            [[maybe_unused]] Lock&& lock,
                                            Transaction& transaction)
{
    assert(lock.mutex() == &mDatabase);
    assert(lock.owns_lock());

    // Remove this file from the database.
    auto query = transaction.query(mQueries.mRemoveFile);

    query.param(":id").set(id);
    query.execute();

    // Deallocate the file's ID if necessary.
    if (synthetic(id))
        deallocateID(id, std::forward<Lock>(lock), transaction);
}

template<typename Lock, typename T>
bool FileServiceContext::removeFromIndex(FileID id,
                                         [[maybe_unused]] Lock&& lock,
                                         FromFileIDMap<T>& map)
{
    assert(lock.mutex() == &mLock);
    assert(lock.owns_lock());

    // Is id in the map?
    auto entry = map.find(id);

    // id's not in the map.
    if (entry == map.end())
        return false;

    // id's in the map but refers a different instance.
    if (!entry->second.expired())
        return false;

    // Remove id from the map.
    map.erase(id);

    // Let the caller know id was removed.
    return true;
}

template<typename T>
bool FileServiceContext::removeFromIndex(FileID id, FromFileIDMap<T>& map)
{
    return removeFromIndex(id, UniqueLock(mLock), map);
}

template<typename Lock, typename Transaction>
auto FileServiceContext::storageInfo([[maybe_unused]] Lock&& lock,
                                     const ReclaimOptions& options,
                                     Transaction&& transaction) -> StorageInfo
{
    // Sanity.
    assert(lock.mutex() == &mDatabase);
    assert(lock.owns_lock());
    assert(transaction.inProgress());

    // Convenience.
    const auto ageThreshold = options.mAgeThreshold;
    const auto reclaimTarget = options.mReclaimTarget;

    // Compute the storage used by all files in storage.
    auto query = transaction.query(mQueries.mGetStorageInfo);

    // Compute maximum reclaimable access time.
    auto accessed = system_clock::to_time_t(system_clock::now() - ageThreshold);
    query.param(":accessed").set(accessed);

    query.param(":target_remaining_size").set(reclaimTarget);

    query.execute();

    // Sanity.
    assert(query);

    // Populate values
    StorageInfo storageInfo;
    storageInfo.mReclaimableSize = query.field("size_to_reclaim").template get<std::uint64_t>();
    storageInfo.mAllocatedSize = query.field("total_allocated_size").template get<std::uint64_t>();
    storageInfo.mReportedSize = query.field("total_reported_size").template get<std::uint64_t>();
    storageInfo.mTotalSize = query.field("total_size").template get<std::uint64_t>();

    return storageInfo;
}

template<typename Lock, typename Transaction>
auto FileServiceContext::storageUsed([[maybe_unused]] Lock&& lock, Transaction&& transaction)
    -> std::uint64_t
{
    // Sanity.
    assert(lock.mutex() == &mDatabase);
    assert(lock.owns_lock());
    assert(transaction.inProgress());

    // Compute the storage used by all files in storage.
    auto query = transaction.query(mQueries.mGetStorageUsed);

    query.execute();

    // Sanity.
    assert(query);

    // Return the total allocated size of all files in storage.
    return query.field("total_allocated_size").template get<std::uint64_t>();
}

void FileServiceContext::updated(NodeEventQueue& events)
{
    // Process the latest changes from the cloud.
    EventProcessor (*this)(events);
}

FileServiceContext::FileServiceContext(Client& client,
                                       FileService& service,
                                       const UserStoragePath& userStoragePath):
    NodeEventObserver(),
    mInstanceLogger("FileServiceContext", *this, logger()),
    mClient(client),
    mDeinitialized{false},
    mStorage(userStoragePath),
    mDatabase(createDatabase(mStorage.databasePath())),
    mQueries(mDatabase),
    mCacheCleaner(),
    mFileContexts(),
    mInfoContextRemoved(),
    mInfoContexts(),
    mLocationProviders(),
    mLocationProvidersLock(),
    mLock(),
    mReclaimContext(),
    mReclaimContextLock(),
    mReclaimTask(),
    mReclaimTaskLock(),
    mEventEmitter(),
    mService(service),
    mExecutor(TaskExecutorFlags(), logger()),
    mContextMonitor(),
    mReclaimMonitor()
{
    // Let the client know we want to receive node change events.
    mClient.addEventObserver(*this);

    // Purge any lingering removed files.
    purgeRemovedFiles();

    // Get our hands on the service's reclamation options.
    auto options = service.reclaimOptions();

    // User hasn't specified any storage quota.
    if (!options.mReclaimThreshold)
        return;

    // Assume user's specified an initial reclamation delay.
    auto delay = options.mDelay;

    // User hasn't specified an initial reclamation delay.
    if (!delay.count())
        delay = options.mPeriod;

    // User hasn't specified a reclamation period.
    if (!delay.count())
        return;

    // When should we perform the reclamation?
    auto when = steady_clock::now() + delay;

    FSInfoF("reclaim task is scheduled in %d seconds", delay.count());

    // Schedule initial reclamation for later execution.
    mReclaimTask = mExecutor.execute(std::bind(&FileServiceContext::reclaimTaskCallback,
                                               this,
                                               mReclaimMonitor.begin(),
                                               when,
                                               std::placeholders::_1),
                                     when,
                                     true);
}

FileServiceContext::~FileServiceContext()
{
    // Let any active file contexts know we're shutting down.
    mDeinitialized = true;

    // Cancel any scheduled reclamations.
    reclaimOptionsChanged(ReclaimOptions());

    // Make sure any pending reclamations have completed.
    mReclaimMonitor.waitUntilIdle();

    // Cancel in-memory pins that might be keeping contexts alive.
    {
        // So we can safely iterate over mFileContexts.
        std::lock_guard guard(mLock);

        // Cancel any in-memory pins present on our contexts.
        for (auto i = mFileContexts.begin(); i != mFileContexts.end();)
        {
            // Eagerly move to the next context, if any.
            auto context = i++->second.lock();

            // Context has already been purged from memory.
            if (!context)
                continue;

            // Some time in the distant past.
            auto when = std::chrono::steady_clock::time_point::min();

            // Cancel any in-memory pins on the context.
            context->pinUntil(context, when);
        }
    }

    // Let the client know we're no longer interested in node events.
    mClient.removeEventObserver(*this);
}

auto FileServiceContext::add(NodeHandle handle,
                             const NodeKeyData& keyData,
                             std::uint64_t size) -> FileServiceResultOr<FileID>
try
{
    // Caller's given us a bogus key.
    if (keyData.mKeyAndIV.size() != FILENODEKEYLENGTH)
        return unexpected(FILE_SERVICE_INVALID_FILE_KEY);

    // Acquire context lock.
    std::lock_guard lockContexts(mLock);

    // Acquire database lock.
    std::lock_guard lockDatabase(mDatabase);

    // So we can safely access the database.
    auto transaction = mDatabase.transaction();

    // Check if this file's already in the database.
    auto query = transaction.query(mQueries.mGetFile);

    query.param(":id").set(handle);

    // File's already in the database.
    if (query.execute())
        return unexpected(FILE_SERVICE_FILE_ALREADY_EXISTS);

    // Convenience.
    auto accessed = now();
    auto id = FileID::from(handle);

    // Add the file to the database.
    query = transaction.query(mQueries.mAddFile);

    query.param(":accessed").set(accessed);
    query.param(":allocated_size").set(0u);
    query.param(":dirty").set(false);
    query.param(":handle").set(handle);
    query.param(":id").set(id);
    query.param(":modified").set(accessed);
    query.param(":removed").set(false);
    query.param(":reported_size").set(0u);
    query.param(":size").set(size);

    query.execute();

    // Add the file's key data to the database.
    query = transaction.query(mQueries.mAddFileKeyData);

    query.param(":chat_auth").set(keyData.mChatAuth);
    query.param(":id").set(id);
    query.param(":is_public").set(keyData.mIsPublicHandle);
    query.param(":key_and_iv").set(keyData.mKeyAndIV);
    query.param(":private_auth").set(keyData.mPrivateAuth);
    query.param(":public_auth").set(keyData.mPublicAuth);

    query.execute();

    // Add the file to storage.
    mStorage.addFile(id);

    // Persist database changes.
    transaction.commit();

    // Let the caller know the foreign file was added successfully.
    return id;
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to add foreign file to service: %s", exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

FileLocationProviderID FileServiceContext::addLocationProvider(FileLocationProvider provider)
{
    // Provides a unique ID for added registered location provider.
    static std::atomic<std::uint64_t> id{0};

    // Sanity: provider should never be null.
    assert(provider);

    // Acquire location provider map lock.
    UniqueLock guard(mLocationProvidersLock);

    // Add the location provider to our map.
    auto [iterator, added] =
        mLocationProviders.emplace(std::piecewise_construct,
                                   std::make_tuple(FileLocationProviderID{id++}),
                                   std::forward_as_tuple(std::move(provider)));

    // Sanity: adding a provider to our map should never fail.
    assert(added);

    // Silence compiler.
    static_cast<void>(added);

    // Return the provider's ID to our caller.
    return iterator->first;
}

FileEventObserverID FileServiceContext::addObserver(FileEventObserver observer)
{
    return mEventEmitter.addObserver(std::move(observer));
}

void FileServiceContext::cleanCacheOnDestruction()
{
    mCacheCleaner = [this]()
    {
        cleanCache();
    };
}

Client& FileServiceContext::client()
{
    return mClient;
}

auto FileServiceContext::create() -> FileServiceResultOr<File>
try
{
    // Acquire context and database locks.
    UniqueLock lockContexts(mLock);
    UniqueLock lockDatabase(mDatabase);

    // Initiate a transaction so we can safely modify the database.
    auto transaction = mDatabase.transaction();

    // Try and allocate a new file ID.
    auto id = allocateID(lockDatabase, transaction);

    // Compute the new file's modification time.
    auto modified = now();

    // Add a new file to the database.
    auto query = transaction.query(mQueries.mAddFile);

    query.param(":accessed").set(modified);
    query.param(":allocated_size").set(0u);
    query.param(":dirty").set(true);
    query.param(":handle").set(nullptr);
    query.param(":id").set(id);
    query.param(":modified").set(modified);
    query.param(":removed").set(false);
    query.param(":reported_size").set(0u);
    query.param(":size").set(0u);

    query.execute();

    // Clarity.
    auto allocatedSize = 0u;
    auto dirty = true;
    auto duration = std::nullopt;
    auto reportedSize = 0u;
    auto size = 0u;

    // Instantiate an info context to describe our new file.
    auto info = std::make_shared<FileInfoContext>(modified,
                                                  mContextMonitor.begin(),
                                                  allocatedSize,
                                                  dirty,
                                                  duration,
                                                  NodeHandle(),
                                                  id,
                                                  modified,
                                                  reportedSize,
                                                  *this,
                                                  size);

    // Instantiate a file context to manipulate our new file.
    auto file = std::make_shared<FileContext>(mContextMonitor.begin(),
                                              mStorage.addFile(id),
                                              info,
                                              std::nullopt,
                                              FileRangeSet(),
                                              *this);

    // Persist our changes.
    transaction.commit();

    // Add both contexts to our index.
    mFileContexts.emplace(id, file);
    mInfoContexts.emplace(id, std::move(info));

    // Return a file instance to our caller.
    return File(FileServiceContextBadge{}, std::move(file));
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to create a new file: %s", exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

Database& FileServiceContext::database()
{
    return mDatabase;
}

LocalPath FileServiceContext::databasePath() const
{
    return mStorage.databasePath();
}

bool FileServiceContext::deinitializing() const
{
    return mDeinitialized.load();
}

TaskExecutor& FileServiceContext::executor()
{
    return mExecutor;
}

auto FileServiceContext::info(FileID id) -> FileServiceResultOr<FileInfo>
try
{
    // Try and get our hands on this file's info.
    auto maybeInfo = infoContext(id);

    // Couldn't get information.
    if (!maybeInfo)
    {
        // Because the file's been removed.
        if (maybeInfo.error() == FILE_SERVICE_FILE_DOESNT_EXIST)
            return unexpected(FILE_SERVICE_UNKNOWN_FILE);

        // Because we encountered some kind of failure.
        return unexpected(maybeInfo.error());
    }

    // Clarity.
    auto& info = *maybeInfo;

    // File's in storage.
    if (info)
        return FileInfo(FileServiceContextBadge(), std::move(info));

    // File isn't in storage.
    return unexpected(FILE_SERVICE_UNKNOWN_FILE);
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to get file information: %s: %s", toString(id).c_str(), exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

auto FileServiceContext::location(FileID id) -> FileServiceResultOr<FileLocation>
try
{
    // Try and get our hands on the file's info.
    auto maybeInfo = infoContext(id);

    // File's been removed.
    if (!maybeInfo)
        return unexpected(maybeInfo.error());

    auto info = std::move(*maybeInfo);

    // File isn't known to the service.
    if (!info)
        return unexpected(FILE_SERVICE_UNKNOWN_FILE);

    // Acquire location provider map lock.
    SharedLock guard(mLocationProvidersLock);

    // Try and get the file's location from our location providers.
    for (auto& [_, provider]: mLocationProviders)
    {
        // Provider knew where the file is located.
        if (auto location = provider(id))
            return std::move(*location);
    }

    // Assume the file's been removed if we couldn't get its location.
    return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to get file location: %s: %s", toString(id).c_str(), exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

void FileServiceContext::notify(const FileEvent& event)
{
    mEventEmitter.notify(event);
}

auto FileServiceContext::open(FileID id) -> FileServiceResultOr<File>
try
{
    // Check if the file's already been opened.
    auto maybeFile = fileContextFromIndex(id, SharedLock(mLock));

    // File's been marked as removed.
    if (!maybeFile)
        return unexpected(maybeFile.error());

    // File isn't in memory.
    if (!*maybeFile)
        maybeFile = fileContextFromDatabase(id);

    // Couldn't open the file.
    if (!maybeFile)
        return unexpected(maybeFile.error());

    // Return file to our caller.
    return File({}, std::move(*maybeFile));
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to open file: %s: %s", toString(id).c_str(), exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

FileServiceQueries& FileServiceContext::queries()
{
    return mQueries;
}

auto FileServiceContext::purge() -> FileServiceResult
try
{
    // True when there are no info contexts in memory.
    auto idle = [this]()
    {
        return mInfoContexts.empty();
    }; // idle

    // Make sure we have exclusive access to the context.
    UniqueLock lockContexts(mLock);

    // Wait until all info contexts have been removed from memory.
    mInfoContextRemoved.wait(lockContexts, idle);

    // Make sure we have exclusive access to the database.
    UniqueLock lockDatabase(mDatabase);

    // Retrieve the ID of each file in storage.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFileIDs);

    // Purge each file's data from storage.
    for (query.execute(); query; ++query)
        mStorage.removeFile(query.field("id").get<FileID>());

    // Remove all the files from the database.
    query = transaction.query(mQueries.mRemoveFiles);

    query.execute();

    // Remove any synthetic IDs saved for reuse.
    query = transaction.query(mQueries.mRemoveFileIDs);

    query.execute();

    // Reset the ID generator to its initial state.
    query = transaction.query(mQueries.mSetNextFileID);

    query.param(":next").set(0u);
    query.execute();

    // Persist database changes.
    transaction.commit();

    // Let the caller know the service has been purged.
    return FILE_SERVICE_SUCCESS;
}

catch (std::runtime_error& exception)
{
    // Leave a hint as to why we couldn't purge files from the service.
    FSErrorF("Unable to purge files from storage: %s", exception.what());

    // Let the caller know we couldn't purge files from storage.
    return FILE_SERVICE_UNEXPECTED;
}

void FileServiceContext::reclaim(ReclaimCallback callback, const ReclaimOptions& reclaimOptions)
{
    // Acquire reclaim context lock.
    std::unique_lock lock(mReclaimContextLock);

    // Reclamation is already in progress.
    if (mReclaimContext)
        return mReclaimContext->queue(std::move(callback));

    // Instantiate reclaim context.
    auto context = std::make_shared<ReclaimContext>(*this);

    // Make context visible to other callers.
    mReclaimContext = context;

    // Queue callback for later execution.
    mReclaimContext->queue(std::move(callback));

    // Release reclaim context lock.
    lock.unlock();

    // Reclaim storage space.
    mReclaimContext->reclaim(mReclaimContext, reclaimOptions);
}

void FileServiceContext::removeLocationProvider(FileLocationProviderID id)
{
    // Acquire location provider map lock.
    std::lock_guard guard(mLocationProvidersLock);

    // Remove the provider from our map.
    mLocationProviders.erase(id);
}

void FileServiceContext::removeObserver(FileEventObserverID id)
{
    mEventEmitter.removeObserver(id);
}

ReclaimOptions FileServiceContext::reclaimOptions()
{
    return mService.reclaimOptions();
}

void FileServiceContext::reclaimOptionsChanged(const ReclaimOptions& newOptions)
{
    // Acquire task lock.
    UniqueLock taskLock(mReclaimTaskLock);

    // Caller wants to disable periodic reclamation.
    if (!reclamationEnabled(newOptions))
    {
        FSInfo1("Aborting reclaim task as reclamation disabled");

        return mReclaimTask.abort(), void();
    }

    // Periodic reclamation is already scheduled.
    if (mReclaimTask && !mReclaimTask.completed())
    {
        // Leave a trail so we know what we've done.
        FSInfo1("Aborting reclaim task");

        // Abort it
        mReclaimTask.abort();
    }

    // Convenience.
    auto delay = newOptions.mDelay;

    // When should we perform the reclamation?
    auto when = steady_clock::now() + delay;

    FSInfoF("Reclaim task is scheduled to execute in %d seconds", delay.count());

    // Schedule a reclamation for some time in the future.
    mReclaimTask = mExecutor.execute(std::bind(&FileServiceContext::reclaimTaskCallback,
                                               this,
                                               mReclaimMonitor.begin(),
                                               when,
                                               std::placeholders::_1),
                                     when,
                                     true);
}

void FileServiceContext::removeFromIndex(FileContextBadge, FileID id)
{
    removeFromIndex(id, mFileContexts);
}

void FileServiceContext::removeFromIndex(FileInfoContextBadge, FileInfoContext& context)
try
{
    // Latch the file's ID.
    auto id = context.id();

    // Make sure we have exclusive access to mInfoContexts.
    UniqueLock lockContexts(mLock);

    // mInfoContexts contains a distinct info instance for this file.
    if (!removeFromIndex(id, lockContexts, mInfoContexts))
        return;

    // Make sure we have exclusive access to mDatabase.
    UniqueLock lockDatabase(mDatabase);

    // So we can safely modify the database.
    auto transaction = mDatabase.transaction();

    // File hasn't been removed.
    if (!context.removed())
    {
        // Update the file's access time.
        auto query = transaction.query(mQueries.mSetFileAccessTime);

        query.param(":accessed").set(context.accessed());
        query.param(":id").set(id);
        query.execute();

        // Persist database changes.
        transaction.commit();

        // Let waiters know the context's been removed.
        return mInfoContextRemoved.notify_all();
    }

    // Remove the file.
    remove(lockContexts, lockDatabase, id, transaction);

    // Persist our changes.
    transaction.commit();

    // Let waiters know the context's been removed.
    mInfoContextRemoved.notify_all();
}

catch (std::runtime_error& exception)
{
    FSWarningF("Unable to purge %s from storage: %s",
               toString(context.id()).c_str(),
               exception.what());
}

ServiceOptions FileServiceContext::serviceOptions()
{
    return mService.serviceOptions();
}

auto FileServiceContext::storageInfo(const ReclaimOptions* options)
    -> FileServiceResultOr<StorageInfo>
try
{
    // Make sure no one else is changing the database.
    UniqueLock lock(mDatabase);

    // So we can safely access the database.
    auto transaction = mDatabase.transaction();

    // Let the caller know how much storage space we're using.
    if (options)
        return storageInfo(lock, *options, transaction);
    else
        return storageInfo(lock, this->reclaimOptions(), transaction);
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to determine detailed storage size: %s", exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

auto FileServiceContext::storageUsed() -> FileServiceResultOr<std::uint64_t>
try
{
    // Make sure no one else is changing the database.
    UniqueLock lock(mDatabase);

    // So we can safely access the database.
    auto transaction = mDatabase.transaction();

    // Let the caller know how much storage space we're using.
    return storageUsed(lock, transaction);
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to determine storage footprint: %s", exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

LocalPath FileServiceContext::userFilePath(FileID id) const
{
    return mStorage.userFilePath(id);
}

FileServiceContext::EventProcessor::EventProcessor(FileServiceContext& service):
    mService(service),
    mServiceLock(service.mLock),
    mDatabaseLock(service.mDatabase),
    mQueries(service.mQueries),
    mTransaction(service.mDatabase.transaction())
{}

void FileServiceContext::EventProcessor::dispatch(const NodeEvent& event)
try
{
    if (event.type() == NODE_EVENT_REMOVED)
        removed(event);
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to dispatch node event: %s", exception.what());
}

FileInfoContextPtr FileServiceContext::EventProcessor::info(FileID id)
{
    // Check if the file's information is in memory.
    auto info = mService.infoContextFromIndex(id, mServiceLock);

    // File's in memory but has been marked as removed.
    if (info && info->removed())
        return nullptr;

    // Return the file's information to our caller.
    return info;
}

bool FileServiceContext::EventProcessor::mark(FileID id)
{
    // Check if the file's in memory.
    auto info = this->info(id);

    // File's not in memory.
    if (!info)
        return false;

    // Mark the file as removed in the database.
    auto query = mTransaction.query(mQueries.mSetFileRemoved);

    query.param(":id").set(id);
    query.execute();

    // Mark the file as removed in memory.
    info->removed(std::true_type{});

    // Let our caller know we marked an in-memory file.
    return true;
}

void FileServiceContext::EventProcessor::remove(FileID id)
{
    // Remove a file from the database and from storage.
    mService.remove(mServiceLock, mDatabaseLock, id, mTransaction);

    // Let observers know the file's been removed.
    mService.notify(FileRemoveEvent{id});
}

void FileServiceContext::EventProcessor::removed(const NodeEvent& event)
{
    // Directories are not managed by the service.
    if (event.isDirectory())
        return;

    // Is this node managed by the service?
    auto query = mTransaction.query(mQueries.mGetFile);

    query.param(":handle").set(event.handle());
    query.param(":id").set(nullptr);
    query.param(":removed").set(false);

    // Node isn't managed by the service.
    if (!query.execute())
        return;

    // Convenience.
    auto id = query.field("id").get<FileID>();

    // File's not in memory so purge it from the service.
    if (!mark(id))
        remove(id);
}

void FileServiceContext::EventProcessor::operator()(NodeEventQueue& events)
try
{
    // Process each event in turn.
    for (; !events.empty(); events.pop_front())
        dispatch(events.front());

    // Persist any database changes.
    mTransaction.commit();
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to dispatch node events: %s", exception.what());
}

void FileServiceContext::ReclaimContext::reclaim(ReclaimContextPtr context, FileID id)
{
    FSInfoF("Reclaim file started: %s", id.toString().c_str());

    // Sanity.
    assert(context);

    // Try and open the file.
    auto file = mService.open(id);

    // Couldn't open the file.
    if (!file)
        return reclaimed(std::move(context), FILE_FAILED);

    // Try and reclaim the file. file is copied in the lambda object so that file is alive while
    // reclaiming
    file->reclaim(
        [context = std::move(context), f = *file](FileResultOr<std::uint64_t> result)
        {
            context->reclaimed(context, result);
        });
}

template<typename Lock>
void FileServiceContext::ReclaimContext::reclaimBatch(ReclaimContextPtr context,
                                                      [[maybe_unused]] Lock&& lock)
{
    // Sanity.
    assert(context);
    assert(lock.mutex() == &mLock);
    assert(lock.owns_lock());

    // There are no files left to reclaim.
    if (mIDs.empty() && mNumPending == 0)
    {
        // We were able to reclaim some space or encountered no failures.
        if (mReclaimed || mResult == FILE_SERVICE_SUCCESS)
            return completed(mReclaimed);

        // We encountered some kind of failure.
        return completed(unexpected(mResult));
    }

    // How many files should we reclaim at once?
    auto batchSize = mService.reclaimOptions().mBatchSize;

    // Reclaim one or more files.
    while (mNumPending < batchSize && !mIDs.empty())
    {
        // Grab the ID of a file waiting to be reclaimed.
        auto id = mIDs.back();

        mIDs.pop_back();

        // Increment number of pending reclamations.
        ++mNumPending;

        // Try and reclaim the file.
        reclaim(context, id);
    }
}

void FileServiceContext::ReclaimContext::reclaimed(ReclaimContextPtr context,
                                                   FileResultOr<std::uint64_t> result)
{
    FSInfoF("Reclaim file result: %s, %" PRIu64,
            toString(result.errorOr(FILE_SUCCESS)),
            result.valueOr(0));

    // Make sure no one else changes our members.
    std::unique_lock lock(mLock);

    // Sanity.
    assert(context);
    assert(mNumPending);

    // Reduce number of pending reclamations.
    --mNumPending;

    // Update total amount of reclaimed space.
    mReclaimed += result.valueOr(0ul);

    // Remember if we encountered any failures.
    if (!result)
        mResult = FILE_SERVICE_UNEXPECTED;

    // Reclaim remaining files if any.
    reclaimBatch(std::move(context), std::move(lock));
}

FileServiceContext::ReclaimContext::ReclaimContext(FileServiceContext& service):
    mInstanceLogger("FileServiceContext::ReclaimContext", *this, logger()),
    mActivity(service.mReclaimMonitor.begin()),
    mCallbacks(),
    mIDs(),
    mNumPending(0),
    mReclaimed(0),
    mResult(FILE_SERVICE_SUCCESS),
    mService(service)
{}

void FileServiceContext::ReclaimContext::completed(FileServiceResultOr<std::uint64_t> result)
{
    // Let the service know the reclamation has completed.
    {
        std::lock_guard guard(mService.mReclaimContextLock);
        mService.mReclaimContext = nullptr;
    }

    // Execute queued callbacks.
    for (auto& callback: mCallbacks)
        callback(result);
}

void FileServiceContext::ReclaimContext::queue(ReclaimCallback callback)
{
    // Queue the caller's callback for later execution.
    mCallbacks.emplace_back(std::move(callback));
}

void FileServiceContext::ReclaimContext::reclaim(ReclaimContextPtr context,
                                                 const ReclaimOptions& reclaimOptions)
{
    // Try and figure out what files we can reclaim.
    auto ids = mService.reclaimable(reclaimOptions);

    // Couldn't determine how many files to reclaim.
    if (!ids)
        return completed(ids.error());

    FSInfoF("reclaim task is started: %d files", ids->size());

    // Remember what file's we're reclaiming.
    mIDs = std::move(*ids);

    // Reclaim zero or more files in a batch.
    reclaimBatch(std::move(context), std::unique_lock(mLock));
}

Database createDatabase(const LocalPath& databasePath)
{
    Database database(logger(), databasePath);

    DatabaseBuilder(database).build();

    return database;
}

std::optional<std::uint32_t> extractDuration(const std::string& attributes,
                                             const NodeKeyData& keyData)
{
    // No attributes? No duration.
    if (attributes.empty())
        return std::nullopt;

    // Sanity: key data should always be valid.
    assert(keyData.mKeyAndIV.size() == FILENODEKEYLENGTH);

    // Caller's passed us invalid key data.
    if (keyData.mKeyAndIV.size() != FILENODEKEYLENGTH)
        return std::nullopt;

    // Necessary as getMediaProperty(...) requires a mutable file key.
    auto temp = keyData.mKeyAndIV.substr(FILENODEKEYLENGTH / 2);

    // Necessary as getMediaProperty(...) wants the file key as u32s.
    auto fileKey = reinterpret_cast<std::uint32_t*>(temp.data());

    // Keeps line below simple.
    auto selector = &MediaProperties::playtime;

    // Try and extract the node's file duration.
    auto duration = MediaProperties::getMediaProperty(attributes, fileKey, selector);

    // Duration exists and is nonzero.
    if (duration && *duration)
        return duration;

    // Duration doesn't exist or is zero.
    return std::nullopt;
}

bool reclamationEnabled(const ReclaimOptions& options)
{
    return options.mBatchSize && options.mPeriod.count() && options.mReclaimThreshold;
}

} // file_service
} // mega
