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
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_info_context_badge.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/logging.h>
#include <mega/filesystem.h>

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
    // Called when a new node has been added.
    //
    // If the event describes a node we added, ignore it.
    //
    // Otherwise, check if the node would "replace" a file we manage.
    // If so, remove the file.
    //
    // Note that a directory with the same name and parent as some file that
    // we manage will "replace" the file that we manage.
    void added(const NodeEvent& event);

    // Called to dispatch an event.
    void dispatch(const NodeEvent& event);

    // Retrieve a file's info context.
    FileInfoContextPtr info(FileID id);

    // Mark an in-memory file as removed.
    //
    // Returns true iff the file was in memory.
    bool mark(FileID id);

    // Called when a node has been moved or renamed.
    //
    // If the event describes a node that would "replace" a file we manage,
    // remove the file.
    //
    // If the event describe a file that we manage and that file has been
    // superseded by a new version in the cloud, remove it.
    //
    // Otherwise, update the file's location to match the cloud.
    void moved(const NodeEvent& event);

    // Called when a node has been removed.
    //
    // If event describes a directory, delegate to removedDirectory(...).
    //
    // If event describes a file we manage, remove it.
    // Otherwise, ignore the event.
    void removed(const NodeEvent& event);

    // Called when a directory node has been removed.
    //
    // Remove any files associated with the directory described by event.
    //
    // This is necessary as the directory may conceptually contain one or
    // more local files.
    void removedDirectory(const NodeEvent& event);

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
    void reclaim(ReclaimContextPtr context);
}; // ReclaimContext

static Database createDatabase(const LocalPath& databasePath);

static bool reclamationEnabled(const FileServiceOptions& options);

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

auto FileServiceContext::infoFromDatabase(FileID id, bool open) -> InfoContextResult
{
    // Make sure no one is changing our indexes.
    UniqueLock lockContexts(mLock);

    // Make sure no one is changing our database.
    UniqueLock lockDatabase(mDatabase);

    // Check if another thread loaded this file's info.
    auto result = infoFromIndex(id, lockContexts, open);

    // Info was loaded (or marked as removed) by another thread.
    if (!result || result->first)
        return result;

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
        return std::make_pair(nullptr, nullptr);

    // Latch the file's attributes from the database.
    auto accessed = query.field("accessed").get<std::int64_t>();
    auto allocatedSize = query.field("allocated_size").get<std::uint64_t>();
    auto dirty = query.field("dirty").get<bool>();
    auto handle = NodeHandle();
    auto modified = query.field("modified").get<std::int64_t>();
    auto name = query.field("name").get<std::string>();
    auto parent = query.field("parent_handle").get<NodeHandle>();
    auto reportedSize = query.field("reported_size").get<std::uint64_t>();
    auto size = query.field("size").get<std::uint64_t>();

    if (!query.field("handle").null())
        handle = query.field("handle").get<NodeHandle>();

    id = query.field("id").get<FileID>();

    // Convenience.
    FileLocation location{std::move(name), parent};

    // Instantiate a context to represent this file's information.
    auto info = std::make_shared<FileInfoContext>(accessed,
                                                  mActivities.begin(),
                                                  allocatedSize,
                                                  dirty,
                                                  handle,
                                                  id,
                                                  location,
                                                  modified,
                                                  reportedSize,
                                                  *this,
                                                  size);

    // Add the context to our index.
    mInfoContexts.emplace(id, info);

    // Caller isn't interested in the file itself, only its information.
    if (!open)
        return std::make_pair(std::move(info), nullptr);

    // Return the file and its information to our caller.
    return std::make_pair(std::move(info), mStorage.getFile(id));
}

template<typename Lock>
auto FileServiceContext::infoFromIndex(FileID id, Lock&& lock, bool open) -> InfoContextResult
{
    // Check if this file's information is in the index.
    auto info = getFromIndex(id, std::forward<Lock>(lock), mInfoContexts);

    // File's information isn't in the index.
    if (!info)
        return std::make_pair(nullptr, nullptr);

    // File's been removed.
    if (info->removed())
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    // Open the file if requested.
    auto file = open ? mStorage.getFile(id) : nullptr;

    // Return the file and its information to our caller.
    return std::make_pair(std::move(info), std::move(file));
}

auto FileServiceContext::info(FileID id, bool open) -> InfoContextResult
{
    // Check if the file's in memory.
    auto result = infoFromIndex(id, SharedLock(mLock), open);

    // File's in memory or has been removed.
    if (!result || result->first)
        return result;

    // Check if the file's in the database.
    return infoFromDatabase(id, open);
}

auto FileServiceContext::openFromCloud(FileID id) -> FileServiceResultOr<FileContextPtr>
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

    // Make sure no one's changing our indexes.
    UniqueLock lockContexts(mLock);

    // Make sure no one's changing the database.
    UniqueLock lockDatabase(mDatabase);

    // Check if another thread's opened (or removed) this file.
    auto maybeFile = openFromIndex(id, lockContexts);

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
    query.param(":name").set(node->mName);
    query.param(":parent_handle").set(node->mParentHandle);
    query.param(":removed").set(false);
    query.param(":reported_size").set(0u);
    query.param(":size").set(size);

    query.execute();

    // Add the file to storage.
    auto file = mStorage.addFile(id);

    // Persist our database changes.
    transaction.commit();

    // Clarity.
    auto allocatedSize = 0u;
    auto dirty = false;
    auto location = FileLocation{std::move(node->mName), node->mParentHandle};
    auto reportedSize = 0u;

    // Create a context to represent this file's information.
    auto info = std::make_shared<FileInfoContext>(accessed,
                                                  mActivities.begin(),
                                                  allocatedSize,
                                                  dirty,
                                                  node->mHandle,
                                                  id,
                                                  location,
                                                  node->mModified,
                                                  reportedSize,
                                                  *this,
                                                  size);

    // Make sure this file's info is in our index.
    mInfoContexts.emplace(id, info);

    // Create a context to represent the file itself.
    auto context = std::make_shared<FileContext>(mActivities.begin(),
                                                 std::move(file),
                                                 std::move(info),
                                                 FileRangeVector(),
                                                 *this);

    // Make sure the file is in our index.
    mFileContexts.emplace(id, context);

    // Return the file to our caller.
    return context;
}

auto FileServiceContext::openFromDatabase(FileID id) -> FileServiceResultOr<FileContextPtr>
{
    // Try and get our hands on the file's information.
    auto maybeInfo = this->info(id, true);

    // File's been removed.
    if (!maybeInfo)
        return unexpected(maybeInfo.error());

    // Clarity.
    auto& [info, file] = *maybeInfo;

    // File isn't in storage so open it from the cloud.
    if (!info)
        return openFromCloud(id);

    // Acquire lock.
    UniqueLock lock(mLock);

    // File's been removed.
    if (info->removed())
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    // Check if another thread opened the file.
    auto maybeFile = openFromIndex(info->id(), lock);

    // File was opened (or removed) by another thread.
    if (!maybeFile || *maybeFile)
        return maybeFile;

    // What ranges of the file have we already downloaded?
    auto maybeRanges = rangesFromDatabase(id, lock);

    // Shouldn't be possible but handle it just for safety.
    if (!maybeRanges)
        return unexpected(maybeRanges.error());

    // Convenience.
    auto ranges = std::move(*maybeRanges).value_or(FileRangeVector());

    // Instantiate a new file context.
    auto context = std::make_shared<FileContext>(mActivities.begin(),
                                                 std::move(file),
                                                 std::move(info),
                                                 ranges,
                                                 *this);

    // Add the context to our index.
    mFileContexts.emplace(id, context);

    // Return the context to our caller.
    return context;
}

template<typename Lock>
auto FileServiceContext::openFromIndex(FileID id, Lock&& lock)
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

template<typename Lock>
auto FileServiceContext::rangesFromDatabase(FileID id, Lock&& lock) -> RangesResult
{
    // Acquire database lock.
    UniqueLock lockDatabase(mDatabase);

    // Check if another thread opened the file.
    auto maybeRanges = rangesFromIndex(id, lock);

    // Another thread opened (or removed) the file.
    if (!maybeRanges || *maybeRanges)
        return maybeRanges;

    // Check if the file's present in the database.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFile);

    query.param(":handle").set(nullptr);
    query.param(":id").set(id);

    if (!synthetic(id))
        query.param(":handle").set(id.toHandle());

    query.execute();

    // File's not in the database.
    if (!query)
        return unexpected(FILE_SERVICE_UNKNOWN_FILE);

    // File's been removed.
    if (query.field("removed").get<bool>())
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    // Latch ID as it may be distinct from what we were passed.
    id = query.field("id").get<FileID>();

    // Read the ranges from the database.
    query = transaction.query(mQueries.mGetFileRanges);

    query.param(":id").set(id);
    query.execute();

    FileRangeVector ranges;

    for (; query; ++query)
    {
        // Convenience.
        auto begin = query.field("begin").get<std::uint64_t>();
        auto end = query.field("end").get<std::uint64_t>();

        // Add the range to our vector.
        ranges.emplace_back(begin, end);
    }

    // Return ranges to our caller.
    return ranges;
}

template<typename Lock>
auto FileServiceContext::rangesFromIndex(FileID id, Lock&& lock) -> RangesResult
{
    // Check if the file's in memory.
    auto maybeFile = openFromIndex(id, lock);

    // File's in memory and has been marked as removed.
    if (!maybeFile)
        return unexpected(maybeFile.error());

    // File's not in memory.
    if (!*maybeFile)
        return std::nullopt;

    // File's in memory so return its ranges directory.
    return (*maybeFile)->ranges();
}

void FileServiceContext::reclaimTaskCallback(Activity& activity,
                                             steady_clock::time_point when,
                                             const Task& task)
{
    // Exchange mReclaimTask with task.
    auto exchange = [this](Task task)
    {
        // Acquire task lock.
        std::lock_guard guard(mReclaimTaskLock);

        // For ADL lookup.
        using std::swap;

        // Exchange the task.
        swap(mReclaimTask, task);
    }; // exchange

    // Client's shutting down or reclamation has been disabled.
    if (task.aborted())
        return exchange(Task());

    // Schedule another reclamation in the future.
    auto reschedule = [activity = std::move(activity), exchange, this]()
    {
        // Get the service's current options.
        auto options = this->options();

        // Reclamation has been disabled.
        if (!reclamationEnabled(options))
            return exchange(Task());

        // When should the next reclamation occur?
        auto when = steady_clock::now() + options.mReclaimPeriod;

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
    reclaim(std::move(reclaimed));
}

auto FileServiceContext::reclaimable() -> FileServiceResultOr<FileIDVector>
try
{
    // Get our hands on our current options.
    auto options = this->options();

    // Convenience.
    auto sizeThreshold = options.mReclaimSizeThreshold;

    // No quota? No need to reclaim anything.
    if (!sizeThreshold)
        return FileIDVector();

    // So we have exclusive access to the database.
    UniqueLock lock(mDatabase);

    // So we can safely access the database.
    auto transaction = mDatabase.transaction();

    // Figure out how much storage we're currently using.
    auto used = storageUsed(lock, transaction);

    // No need to reclaim any storage.
    if (sizeThreshold >= used)
        return FileIDVector();

    // Get the allocated size and ID of all files in storage.
    auto query = transaction.query(mQueries.mGetReclaimableFiles);

    // Retrieve access time threshold.
    auto threshold = options.mReclaimAgeThreshold;

    // Compute maximum reclaimable access time.
    auto accessed = system_clock::now() - threshold;

    // Specify maximum reclaimable access time.
    query.param(":accessed").set(system_clock::to_time_t(accessed));

    // Tracks the IDs of the files we can reclaim.
    FileIDVector ids;

    // Collect as many IDs for reclamation as necessary.
    for (query.execute(); query && used > sizeThreshold; ++query)
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

FileServiceContext::FileServiceContext(Client& client, const FileServiceOptions& options):
    NodeEventObserver(),
    mClient(client),
    mStorage(mClient),
    mDatabase(createDatabase(mStorage.databasePath())),
    mQueries(mDatabase),
    mFileContexts(),
    mInfoContextRemoved(),
    mInfoContexts(),
    mLock(),
    mOptions(options),
    mOptionsLock(),
    mReclaimContext(),
    mReclaimContextLock(),
    mReclaimTask(),
    mReclaimTaskLock(),
    mActivities(),
    mExecutor(TaskExecutorFlags(), logger())
{
    // Let the client know we want to receive node change events.
    mClient.addEventObserver(*this);

    // Purge any lingering removed files.
    purgeRemovedFiles();

    // User hasn't specified any storage quota.
    if (!mOptions.mReclaimSizeThreshold)
        return;

    // Assume user's specified an initial reclamation delay.
    auto delay = mOptions.mReclaimDelay;

    // User hasn't specified an initial reclamation delay.
    if (!delay.count())
        delay = mOptions.mReclaimPeriod;

    // User hasn't specified a reclamation period.
    if (!delay.count())
        return;

    // When should we perform the reclamation?
    auto when = steady_clock::now() + delay;

    // Schedule initial reclamation for later execution.
    mReclaimTask = mExecutor.execute(std::bind(&FileServiceContext::reclaimTaskCallback,
                                               this,
                                               mActivities.begin(),
                                               when,
                                               std::placeholders::_1),
                                     when,
                                     true);
}

FileServiceContext::~FileServiceContext()
{
    // Let the client know we're no longer interested in node events.
    mClient.removeEventObserver(*this);
}

Client& FileServiceContext::client()
{
    return mClient;
}

auto FileServiceContext::create(NodeHandle parent, const std::string& name)
    -> FileServiceResultOr<File>
try
{
    // The caller's passed us an invalid name.
    if (name.empty())
        return unexpected(FILE_SERVICE_INVALID_NAME);

    // Parent already contains a child with this name.
    if (auto node = mClient.get(parent, name))
        return unexpected(FILE_SERVICE_FILE_ALREADY_EXISTS);

    // Try and get information about parent.
    auto node = mClient.get(parent);

    // Couldn't get information about parent.
    if (!node)
    {
        // Because we encountered some unexpected error.
        if (node.error() != API_ENOENT)
            return unexpected(FILE_SERVICE_UNEXPECTED);

        // Because it doesn't exist.
        return unexpected(FILE_SERVICE_PARENT_DOESNT_EXIST);
    }

    // Parent isn't a directory.
    if (!node->mIsDirectory)
        return unexpected(FILE_SERVICE_PARENT_IS_A_FILE);

    // Acquire context and database locks.
    UniqueLock lockContexts(mLock);
    UniqueLock lockDatabase(mDatabase);

    // Initiate a transaction so we can safely modify the database.
    auto transaction = mDatabase.transaction();

    // Check if parent already contains a local child with this name.
    auto query = transaction.query(mQueries.mGetFileByNameAndParentHandle);

    query.param(":parent_handle").set(parent);
    query.param(":name").set(name);

    query.execute();

    // Parent already contains a local child with this name.
    if (query)
        return unexpected(FILE_SERVICE_FILE_ALREADY_EXISTS);

    // Try and allocate a new file ID.
    auto id = allocateID(lockDatabase, transaction);

    // Compute the new file's modification time.
    auto modified = now();

    // Add a new file to the database.
    query = transaction.query(mQueries.mAddFile);

    query.param(":accessed").set(modified);
    query.param(":allocated_size").set(0u);
    query.param(":dirty").set(true);
    query.param(":handle").set(nullptr);
    query.param(":id").set(id);
    query.param(":modified").set(modified);
    query.param(":name").set(name);
    query.param(":parent_handle").set(parent);
    query.param(":removed").set(false);
    query.param(":reported_size").set(0u);
    query.param(":size").set(0u);

    query.execute();

    // Clarity.
    auto allocatedSize = 0u;
    auto dirty = true;
    auto location = FileLocation{name, parent};
    auto reportedSize = 0u;
    auto size = 0u;

    // Instantiate an info context to describe our new file.
    auto info = std::make_shared<FileInfoContext>(modified,
                                                  mActivities.begin(),
                                                  allocatedSize,
                                                  dirty,
                                                  NodeHandle(),
                                                  id,
                                                  location,
                                                  modified,
                                                  reportedSize,
                                                  *this,
                                                  size);

    // Instantiate a file context to manipulate our new file.
    auto file = std::make_shared<FileContext>(mActivities.begin(),
                                              mStorage.addFile(id),
                                              info,
                                              FileRangeVector(),
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

auto FileServiceContext::execute(std::function<void(const Task&)> function) -> Task
{
    return mExecutor.execute(std::move(function), true);
}

auto FileServiceContext::info(FileID id) -> FileServiceResultOr<FileInfo>
try
{
    // Try and get our hands on this file's info.
    auto maybeInfo = info(id, false);

    // File's been removed.
    if (!maybeInfo)
        return unexpected(maybeInfo.error());

    // Clarity.
    auto& [info, _] = *maybeInfo;

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

auto FileServiceContext::open(FileID id) -> FileServiceResultOr<File>
try
{
    // Check if the file's already been opened.
    auto maybeFile = openFromIndex(id, SharedLock(mLock));

    // File's been marked as removed.
    if (!maybeFile)
        return unexpected(maybeFile.error());

    // File isn't in memory.
    if (!*maybeFile)
        maybeFile = openFromDatabase(id);

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

void FileServiceContext::options(const FileServiceOptions& options)
{
    // Set later to prevent modification to our options.
    SharedLock readLock(mOptionsLock, std::defer_lock);

    // Keeps track of our original reclamation period.
    seconds oldPeriod;

    // Update our options.
    {
        // Make sure no one else is modifying our options.
        UniqueLock writeLock(mOptionsLock);

        // Latch current reclamation period.
        oldPeriod = mOptions.mReclaimPeriod;

        // Update our options.
        mOptions = options;

        // Let other threads read our options.
        readLock = writeLock.to_shared_lock();
    }

    // Acquire task lock.
    UniqueLock taskLock(mReclaimTaskLock);

    // Caller wants to disable periodic reclamation.
    if (!reclamationEnabled(options))
        return mReclaimTask.abort(), void();

    // Convenience.
    auto newPeriod = options.mReclaimPeriod;

    // Caller isn't changing reclamation period.
    if (newPeriod == oldPeriod)
        return;

    // Periodic reclamation is already scheduled.
    //
    // Send it a cancellation so it reschedules itself.
    if (mReclaimTask)
        return mReclaimTask.cancel(), void();

    // When should we perform the reclamation?
    auto when = steady_clock::now() + newPeriod;

    // Schedule a reclamation for some time in the future.
    mReclaimTask = mExecutor.execute(std::bind(&FileServiceContext::reclaimTaskCallback,
                                               this,
                                               mActivities.begin(),
                                               when,
                                               std::placeholders::_1),
                                     when,
                                     true);
}

FileServiceOptions FileServiceContext::options()
{
    // Make sure no one else is modifying our options.
    SharedLock guard(mOptionsLock);

    // Return current options to our caller.
    return mOptions;
}

LocalPath FileServiceContext::path(FileID id) const
{
    return mStorage.userFilePath(id);
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

auto FileServiceContext::ranges(FileID id) -> FileServiceResultOr<FileRangeVector>
try
{
    // Try and get the ranges from the index.
    auto maybeRanges = rangesFromIndex(id, SharedLock(mLock));

    // File isn't in memory.
    if (maybeRanges && !*maybeRanges)
        maybeRanges = rangesFromDatabase(id, UniqueLock(mLock));

    // File exists and hasn't been removed.
    if (maybeRanges)
        return maybeRanges->value_or(FileRangeVector());

    // File doesn't exist or has been removed.
    return unexpected(maybeRanges.error());
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to retrieve file ranges: %s: %s", toString(id).c_str(), exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

void FileServiceContext::reclaim(ReclaimCallback callback)
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
    mReclaimContext->reclaim(mReclaimContext);
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

auto FileServiceContext::storageUsed() -> FileServiceResultOr<std::uint64_t>
try
{
    return storageUsed(UniqueLock(mDatabase), mDatabase.transaction());
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to determine storage footprint: %s", exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

FileServiceContext::EventProcessor::EventProcessor(FileServiceContext& service):
    mService(service),
    mServiceLock(service.mLock),
    mDatabaseLock(service.mDatabase),
    mQueries(service.mQueries),
    mTransaction(service.mDatabase.transaction())
{}

void FileServiceContext::EventProcessor::added(const NodeEvent& event)
{
    // Does this node replace a file managed by the service?
    auto query = mTransaction.query(mQueries.mGetFileByNameAndParentHandle);

    query.param(":name").set(event.name());
    query.param(":parent_handle").set(event.parentHandle());
    query.execute();

    // Node doesn't replace any file managed by the service.
    if (!query)
        return;

    // Latch the file's handle, if any.
    NodeHandle handle;

    if (!query.field("handle").null())
        handle = query.field("handle").get<NodeHandle>();

    // Node describes a file managed by the service.
    if (event.handle() == handle)
        return;

    // Latch the file's ID.
    auto id = query.field("id").get<FileID>();

    // File's in memory and has been marked as removed.
    if (mark(id))
        return;

    // The file's not in memory so purge it from the service.
    mService.remove(mServiceLock, mDatabaseLock, id, mTransaction);
}

void FileServiceContext::EventProcessor::dispatch(const NodeEvent& event)
try
{
    switch (event.type())
    {
        case NODE_EVENT_ADDED:
            added(event);
            break;
        case NODE_EVENT_MOVED:
            moved(event);
            break;
        case NODE_EVENT_REMOVED:
            removed(event);
            break;
        default:
            break;
    }
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to dispatch node event: %s", exception.what());
}

FileInfoContextPtr FileServiceContext::EventProcessor::info(FileID id)
{
    // Check if the file's info is in memory.
    auto maybeInfo = mService.infoFromIndex(id, mServiceLock, false);

    // File's in memory but has been marked as removed.
    if (!maybeInfo)
        return nullptr;

    // File may or may not be in memory.
    return maybeInfo->first;
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
    info->removed(true);

    // Let our caller know we marked an in-memory file.
    return true;
}

void FileServiceContext::EventProcessor::moved(const NodeEvent& event)
{
    // Mark or remove a file managed by the service.
    auto remove = [this](FileID id)
    {
        // File's in memory and has been marked as removed.
        if (mark(id))
            return;

        // File's not in memory so remove it immediately.
        mService.remove(mServiceLock, mDatabaseLock, id, mTransaction);
    }; // remove

    // Convenience.
    auto name = event.name();
    auto parentHandle = event.parentHandle();

    // Check if this node would replace a file managed by the service.
    auto query = mTransaction.query(mQueries.mGetFileByNameAndParentHandle);

    query.param(":name").set(name);
    query.param(":parent_handle").set(parentHandle);

    // Node replaces a file managed by the service.
    //
    // do...while used here for control flow.
    do
    {
        if (!query.execute())
            break;

        // Latch the file's ID.
        auto id = query.field("id").get<FileID>();

        // Mark or remove the file.
        remove(id);
    }
    while (0);

    // Node's a directory so it can't be managed by the service.
    if (event.isDirectory())
        return;

    // Check if this node *is* a file managed by the service.
    query = mTransaction.query(mQueries.mGetFile);

    query.param(":handle").set(event.handle());
    query.execute();

    // Node isn't a file managed by the service.
    if (!query)
        return;

    // Latch the file's ID.
    auto id = query.field("id").get<FileID>();

    // Try and locate the node's parent.
    auto parent = mService.mClient.get(parentHandle);

    // Node's been superseded by another version.
    if (parent && !parent->mIsDirectory)
        return remove(id);

    // Check if the file's in memory.
    auto info = this->info(id);

    // File's in memory so update it's in-memory location.
    if (info)
        info->location(FileLocation{name, parentHandle});

    // Update the file's location in the database.
    query = mTransaction.query(mQueries.mSetFileLocation);

    query.param(":id").set(id);
    query.param(":name").set(name);
    query.param(":parent_handle").set(parentHandle);
    query.execute();
}

void FileServiceContext::EventProcessor::removed(const NodeEvent& event)
{
    // Directories are not managed by the service.
    if (event.isDirectory())
        return removedDirectory(event);

    // Is this node managed by the service?
    auto query = mTransaction.query(mQueries.mGetFile);

    query.param(":handle").set(event.handle());
    query.param(":id").set(nullptr);
    query.param(":removed").set(false);
    query.execute();

    // Node isn't managed by the service.
    if (!query)
        return;

    // Convenience.
    auto id = query.field("id").get<FileID>();

    // File's in memory and has been marked as removed.
    if (mark(id))
        return;

    // File's not in memory so purge it from the service.
    mService.remove(mServiceLock, mDatabaseLock, id, mTransaction);
}

void FileServiceContext::EventProcessor::removedDirectory(const NodeEvent& event)
{
    // IDs of children we should mark as removed.
    FileIDVector mark;

    // IDs of children we should remove immediately.
    FileIDVector remove;

    // Retrieve the ID of each child under this directory.
    auto query = mTransaction.query(mQueries.mGetFileIDsByParentHandle);

    query.param(":parent_handle").set(event.handle());

    // Iterate over this directory's children.
    for (query.execute(); query; ++query)
    {
        // Latch the child's ID.
        auto id = query.field("id").get<FileID>();

        // Check if the child's in memory.
        auto info = this->info(id);

        // Child's in memory.
        if (info)
        {
            // Mark the file as removed in memory.
            info->removed(true);

            // Remember to mark this child in the database.
            mark.emplace_back(id);

            // Move onto the next child.
            continue;
        }

        // Child's not in memory: remember to remove it.
        remove.emplace_back(id);
    }

    query = mTransaction.query(mQueries.mSetFileRemoved);

    // Mark in-memory children as removed in the database.
    for (auto id: mark)
    {
        query.param(":id").set(id);
        query.execute();
    }

    // Remove out-of-memory children from the service.
    for (auto id: remove)
        mService.remove(mServiceLock, mDatabaseLock, id, mTransaction);
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
    // Sanity.
    assert(context);

    // Try and open the file.
    auto file = mService.open(id);

    // Couldn't open the file.
    if (!file)
        return reclaimed(std::move(context), FILE_FAILED);

    // So we can use our reclaimed function as a callback.
    auto reclaimed =
        std::bind(&ReclaimContext::reclaimed, this, std::move(context), std::placeholders::_1);

    // Try and reclaim the file.
    file->reclaim(std::move(reclaimed));
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
    if (mIDs.empty())
    {
        // We were able to reclaim some space or encountered no failures.
        if (mReclaimed || mResult == FILE_SERVICE_SUCCESS)
            return completed(mReclaimed);

        // We encountered some kind of failure.
        return completed(mResult);
    }

    // How many files should we reclaim at once?
    auto batchSize = mService.options().mReclaimBatchSize;

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

    // Reclaim remaning files if any.
    reclaimBatch(std::move(context), std::move(lock));
}

FileServiceContext::ReclaimContext::ReclaimContext(FileServiceContext& service):
    mActivity(service.mActivities.begin()),
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

void FileServiceContext::ReclaimContext::reclaim(ReclaimContextPtr context)
{
    // Try and figure out what files we can reclaim.
    auto ids = mService.reclaimable();

    // Couldn't determine how many files to reclaim.
    if (!ids)
        return completed(ids.error());

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

bool reclamationEnabled(const FileServiceOptions& options)
{
    return options.mReclaimBatchSize && options.mReclaimPeriod.count() &&
           options.mReclaimSizeThreshold;
}

} // file_service
} // mega
