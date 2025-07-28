#include <mega/common/client.h>
#include <mega/common/lock.h>
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

class FileServiceContext::ReclaimContext: public std::enable_shared_from_this<ReclaimContext>
{
    // Reclaim a single file.
    void reclaim(FileID id);

    // Called when a file has been reclaimed.
    void reclaimed(FileResultOr<std::uint64_t> result);

    // Make sure our service stays alive as long as we do.
    Activity mActivity;

    // Who should we call when reclamation completes?
    std::vector<ReclaimCallback> mCallbacks;

    // Tracks how many reclaim requests have yet to complete.
    std::size_t mCount;

    // Serializes access to mCount, mReclaimed and mResult.
    std::mutex mLock;

    // Tracks how much space we've recovered.
    std::uint64_t mReclaimed;

    // Tracks any failure we might have encountered.
    FileResult mResult;

    // What service are we reclaiming storage for?
    FileServiceContext& mService;

public:
    ReclaimContext(FileServiceContext& service);

    // Called when the reclamation has completed.
    void completed(FileServiceResultOr<std::uint64_t> result);

    // Queue a callback for later execution.
    void queue(ReclaimCallback callback);

    // Reclaim one or more files.
    void reclaim(const std::vector<FileID>& ids);
}; // ReclaimContext

static Database createDatabase(const LocalPath& databasePath);

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
        map.erase(entry);

    // Return instance to caller.
    return instance;
}

auto FileServiceContext::infoFromDatabase(FileID id, bool open)
    -> std::pair<FileInfoContextPtr, FileAccessPtr>
{
    // Make sure no one is changing our indexes.
    UniqueLock lockContexts(mLock);

    // Make sure no one is changing our database.
    UniqueLock lockDatabase(mDatabase);

    // Another thread loaded this file's info while we were acquiring locks.
    if (auto result = infoFromIndex(id, lockContexts, open); result.first)
        return result;

    // Check if this file exists in the database.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFile);

    query.param(":handle").set(nullptr);
    query.param(":id").set(id);

    // The caller's looking up the file by a node handle.
    if (!synthetic(id))
        query.param(":handle").set(id.toHandle());

    query.execute();

    // We know nothing about this file.
    if (!query)
        return {};

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

    // Instantiate a context to represent this file's information.
    auto info = std::make_shared<FileInfoContext>(accessed,
                                                  mActivities.begin(),
                                                  allocatedSize,
                                                  dirty,
                                                  handle,
                                                  id,
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
auto FileServiceContext::infoFromIndex(FileID id, Lock&& lock, bool open)
    -> std::pair<FileInfoContextPtr, FileAccessPtr>
{
    // Check if this file's information is in the index.
    auto info = getFromIndex(id, std::forward<Lock>(lock), mInfoContexts);

    // File's information isn't in the index.
    if (!info)
        return {};

    // Open the file if requested.
    auto file = open ? mStorage.getFile(id) : nullptr;

    // Return the file and its information to our caller.
    return std::make_pair(std::move(info), std::move(file));
}

auto FileServiceContext::info(FileID id, bool open) -> std::pair<FileInfoContextPtr, FileAccessPtr>
{
    if (auto result = infoFromIndex(id, SharedLock(mLock), open); result.first)
        return result;

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

    // Another thread opened this file while we were acquiring locks.
    if (auto context = openFromIndex(id, lockContexts))
        return context;

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
    query.param(":num_references").set(0u);
    query.param(":reported_size").set(0u);
    query.param(":size").set(size);

    query.execute();

    // Add the file to storage.
    auto file = mStorage.addFile(id);

    // Persist our database changes.
    transaction.commit();

    // Create a context to represent this file's information.
    auto info = std::make_shared<FileInfoContext>(accessed,
                                                  mActivities.begin(),
                                                  0u,
                                                  false,
                                                  node->mHandle,
                                                  id,
                                                  node->mModified,
                                                  0u,
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
    auto [info, file] = this->info(id, true);

    // File isn't in storage so open it from the cloud.
    if (!info)
        return openFromCloud(id);

    // Acquire lock.
    UniqueLock lock(mLock);

    // File's was opened while we were waiting for our lock.
    if (auto context = openFromIndex(info->id(), lock))
        return context;

    // What ranges of the file have we already downloaded?
    auto ranges = rangesFromDatabase(id, lock);

    // Instantiate a new file context.
    auto context = std::make_shared<FileContext>(mActivities.begin(),
                                                 std::move(file),
                                                 std::move(info),
                                                 ranges.value_or(FileRangeVector()),
                                                 *this);

    // Add the context to our index.
    mFileContexts.emplace(id, context);

    // Return the context to our caller.
    return context;
}

template<typename Lock>
auto FileServiceContext::openFromIndex(FileID id, Lock&& lock) -> FileContextPtr
{
    return getFromIndex(id, std::forward<Lock>(lock), mFileContexts);
}

template<typename Lock>
auto FileServiceContext::rangesFromDatabase(FileID id, Lock&& lock)
    -> std::optional<FileRangeVector>
{
    // Acquire database lock.
    UniqueLock lockDatabase(mDatabase);

    // File was opened while we were waiting for our locks.
    if (auto ranges = rangesFromIndex(id, lock))
        return std::move(*ranges);

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
        return std::nullopt;

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
auto FileServiceContext::rangesFromIndex(FileID id, Lock&& lock) -> std::optional<FileRangeVector>
{
    // File's in memory so return its ranges directly.
    if (auto file = openFromIndex(id, lock))
        return file->ranges();

    // Let the caller know they need to get the ranges from the database.
    return std::nullopt;
}

void FileServiceContext::reclaim(ReclaimContextPtr context)
try
{
    // Figure out how many files we need to reclaim.
    auto ids = reclaimable(options());

    // No files need to be reclaimed.
    if (ids.empty())
        return context->completed(0u);

    // Try and reclaim the files.
    context->reclaim(ids);
}

catch (std::runtime_error& exception)
{
    // Let debuggers know why we couldn't reclaim storage space.
    FSErrorF("Unable to reclaim storage space: %s", exception.what());

    // Let callers know we couldn't reclaim storage space.
    return context->completed(FILE_SERVICE_UNEXPECTED);
}

void FileServiceContext::reclaimTaskCallback(Activity& activity,
                                             steady_clock::time_point when,
                                             const Task& task)
{
    // Acquire options lock.
    SharedLock optionsLock(mOptionsLock);

    // Acquire task lock.
    UniqueLock taskLock(mReclaimTaskLock);

    // Client's shutting down or reclamation has been disabled.
    if (task.aborted())
        return mReclaimTask.reset(), void();

    // Convenience.
    auto now = steady_clock::now();

    // Perform reclamation if necessary.
    if (now >= when)
        reclaim([](auto) {});

    // Convenience.
    auto period = mOptions.mReclaimPeriod;
    auto sizeThreshold = mOptions.mReclaimSizeThreshold;

    // Reclamation has been disabled.
    if (!period.count() || !sizeThreshold)
        return mReclaimTask.reset(), void();

    // When should we perform the next reclamation?
    when = now + period;

    // Schedule another reclamation for the future.
    mReclaimTask = mExecutor.execute(std::bind(&FileServiceContext::reclaimTaskCallback,
                                               this,
                                               std::move(activity),
                                               when,
                                               std::placeholders::_1),
                                     when,
                                     false);
}

auto FileServiceContext::reclaimable(const FileServiceOptions& options) -> std::vector<FileID>
{
    // Convenience.
    auto sizeThreshold = options.mReclaimSizeThreshold;

    // No need to reclaim any storage.
    if (!sizeThreshold)
        return {};

    // So we have exclusive access to the database.
    UniqueLock lock(mDatabase);

    // So we can safely access the database.
    auto transaction = mDatabase.transaction();

    // Figure out how much storage we're currently using.
    auto used = storageUsed(lock, transaction);

    // No need to reclaim any storage.
    if (sizeThreshold >= used)
        return {};

    // Get the allocated size and ID of all files in storage.
    auto query = transaction.query(mQueries.mGetReclaimableFiles);

    // Retrieve access time threshold.
    auto threshold = options.mReclaimAgeThreshold;

    // Compute maximum reclaimable access time.
    auto accessed = system_clock::now() - threshold;

    // Specify maximum reclaimable access time.
    query.param(":accessed").set(system_clock::to_time_t(accessed));

    // Tracks the IDs of the files we can reclaim.
    std::vector<FileID> ids;

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
auto FileServiceContext::storageUsed(Lock&& lock, Transaction&& transaction) -> std::uint64_t
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

FileServiceContext::FileServiceContext(Client& client, const FileServiceOptions& options):
    mClient(client),
    mStorage(mClient),
    mDatabase(createDatabase(mStorage.databasePath())),
    mQueries(mDatabase),
    mFileContexts(),
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

FileServiceContext::~FileServiceContext() = default;

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
    auto query = transaction.query(mQueries.mGetFileLocationByParentAndName);

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
    query.param(":num_references").set(0u);
    query.param(":reported_size").set(0u);
    query.param(":size").set(0u);

    query.execute();

    // Specify where this file should be uploaded.
    query = transaction.query(mQueries.mAddFileLocation);

    query.param(":id").set(id);
    query.param(":name").set(name);
    query.param(":parent_handle").set(parent);

    query.execute();

    // Instantiate an info context to describe our new file.
    auto info = std::make_shared<FileInfoContext>(modified,
                                                  mActivities.begin(),
                                                  0ul,
                                                  true,
                                                  NodeHandle(),
                                                  id,
                                                  modified,
                                                  0ul,
                                                  *this,
                                                  0ul);

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
    if (auto [context, _] = info(id, false); context)
        return FileInfo(FileServiceContextBadge(), std::move(context));

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
    if (auto context = openFromIndex(id, SharedLock(mLock)))
        return File({}, std::move(context));

    auto context = openFromDatabase(id);

    if (context)
        return File({}, std::move(*context));

    return unexpected(context.error());
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

    // Convenience.
    auto newPeriod = options.mReclaimPeriod;
    auto sizeThreshold = options.mReclaimSizeThreshold;

    // Acquire task lock.
    UniqueLock taskLock(mReclaimTaskLock);

    // Caller wants to disable periodic reclamation.
    if (!newPeriod.count() || !sizeThreshold)
        return mReclaimTask.abort(), void();

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

auto FileServiceContext::ranges(FileID id) -> FileServiceResultOr<FileRangeVector>
try
{
    // Try and get the ranges from the index.
    if (auto ranges = rangesFromIndex(id, SharedLock(mLock)))
        return std::move(*ranges);

    // Try and get the ranges from the database.
    if (auto ranges = rangesFromDatabase(id, UniqueLock(mLock)))
        return std::move(*ranges);

    // File isn't being managed by the service.
    return unexpected(FILE_SERVICE_UNKNOWN_FILE);
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
    reclaim(std::move(context));
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

    // Retrieve this file's reference count.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFileReferences);

    query.param(":id").set(id);
    query.execute();

    // Don't purge the file as it still has references.
    if (query.field("num_references").get<std::uint64_t>())
    {
        // Update the file's last access time.
        query = transaction.query(mQueries.mSetFileAccessTime);

        query.param(":accessed").set(context.accessed());
        query.param(":id").set(id);

        query.execute();

        // Persist our changes.
        return transaction.commit();
    }

    // Remove the file from the database.
    query = transaction.query(mQueries.mRemoveFile);

    query.param(":id").set(id);
    query.execute();

    // Deallocate the ID if it's synthetic.
    if (synthetic(id))
        deallocateID(id, lockDatabase, transaction);

    // Remove the file from storage.
    mStorage.removeFile(id);

    // Persist our changes.
    transaction.commit();
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

void FileServiceContext::ReclaimContext::reclaim(FileID id)
{
    // Try and open the file.
    auto file = mService.open(id);

    // Couldn't open the file.
    if (!file)
        return reclaimed(FILE_FAILED);

    // So we can use our reclaimed function as a callback.
    auto reclaimed =
        std::bind(&ReclaimContext::reclaimed, shared_from_this(), std::placeholders::_1);

    // Try and reclaim the file.
    file->reclaim(std::move(reclaimed));
}

void FileServiceContext::ReclaimContext::reclaimed(FileResultOr<std::uint64_t> result)
{
    // Make sure no one else changes our members.
    std::lock_guard guard(mLock);

    // Update total amount of reclaimed space.
    mReclaimed += result.valueOr(0ul);

    // Don't forget the first failure we encountered.
    if (mResult)
        mResult = result.errorOr(FILE_SUCCESS);

    // Sanity.
    assert(mCount);

    // Some files are still being reclaimed.
    if (--mCount)
        return;

    // We could recover some space or didn't encounter any failures.
    if (mReclaimed || mResult == FILE_SUCCESS)
        return completed(mReclaimed);

    // We encountered a failure and couldn't recover any space.
    completed(mResult);
}

FileServiceContext::ReclaimContext::ReclaimContext(FileServiceContext& service):
    mActivity(service.mActivities.begin()),
    mCallbacks(),
    mCount{0u},
    mResult{FILE_SERVICE_SUCCESS},
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

void FileServiceContext::ReclaimContext::reclaim(const std::vector<FileID>& ids)
{
    // Remember how many files we're reclaiming.
    //
    // No lock should be necessary here as only a single thread should ever
    // call this method at a time.
    mCount = ids.size();

    // Try and reclaim each file.
    for (auto id: ids)
        reclaim(id);
}

Database createDatabase(const LocalPath& databasePath)
{
    Database database(logger(), databasePath);

    DatabaseBuilder(database).build();

    return database;
}

} // file_service
} // mega
