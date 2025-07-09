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
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/logging.h>
#include <mega/filesystem.h>

#include <stdexcept>

namespace mega
{
namespace file_service
{

using namespace common;

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
    UniqueLock lockContexts(mLock);
    UniqueLock lockDatabase(mDatabase);

    if (auto result = infoFromIndex(id, lockContexts, open); result.first)
        return result;

    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFile);

    query.param(":handle").set(nullptr);
    query.param(":id").set(id);

    if (!synthetic(id))
        query.param(":handle").set(id.toHandle());

    query.execute();

    if (!query)
        return {};

    auto dirty = query.field("dirty").get<bool>();
    auto handle = NodeHandle();

    if (!query.field("handle").null())
        handle = query.field("handle").get<NodeHandle>();

    id = query.field("id").get<FileID>();

    auto file = mStorage.getFile(id);

    auto info = std::make_shared<FileInfoContext>(mActivities.begin(),
                                                  dirty,
                                                  handle,
                                                  id,
                                                  query.field("modified").get<std::int64_t>(),
                                                  *this,
                                                  static_cast<std::uint64_t>(file->size));

    mInfoContexts.emplace(id, info);

    if (!open)
        file.reset();

    return std::make_pair(std::move(info), std::move(file));
}

template<typename Lock>
auto FileServiceContext::infoFromIndex(FileID id, Lock&& lock, bool open)
    -> std::pair<FileInfoContextPtr, FileAccessPtr>
{
    auto info = getFromIndex(id, std::forward<Lock>(lock), mInfoContexts);
    auto file = info && open ? mStorage.getFile(id) : nullptr;

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

    // Add the file to the database.
    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mAddFile);

    query.param(":dirty").set(false);
    query.param(":handle").set(node->mHandle);
    query.param(":id").set(id);
    query.param(":modified").set(node->mModified);

    query.execute();

    // Add the file to storage.
    auto file = mStorage.addFile(*node);

    // Persist our database changes.
    transaction.commit();

    // Create a context to represent this file's information.
    auto info = std::make_shared<FileInfoContext>(mActivities.begin(),
                                                  false,
                                                  node->mHandle,
                                                  id,
                                                  node->mModified,
                                                  *this,
                                                  static_cast<std::uint64_t>(node->mSize));

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
    mActivities(),
    mExecutor(TaskExecutorFlags(), logger())
{}

FileServiceContext::~FileServiceContext() = default;

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

    // Try and add a new file to the database.
    {
        auto query = transaction.query(mQueries.mAddFile);

        query.param(":dirty").set(true);
        query.param(":handle").set(nullptr);
        query.param(":id").set(id);
        query.param(":modified").set(modified);

        query.execute();
    }

    // Instantiate an info context to describe our new file.
    auto info = std::make_shared<FileInfoContext>(mActivities.begin(),
                                                  true,
                                                  NodeHandle(),
                                                  id,
                                                  modified,
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
    UniqueLock guard(mOptionsLock);

    mOptions = options;
}

FileServiceOptions FileServiceContext::options()
{
    SharedLock guard(mOptionsLock);

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

void FileServiceContext::removeFromIndex(FileContextBadge, FileID id)
{
    removeFromIndex(id, mFileContexts);
}

void FileServiceContext::removeFromIndex(FileInfoContextBadge, FileID id)
try
{
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
        return;

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
    FSWarningF("Unable to purge %s from storage: %s", toString(id).c_str(), exception.what());
}

Database createDatabase(const LocalPath& databasePath)
{
    Database database(logger(), databasePath);

    DatabaseBuilder(database).build();

    return database;
}

} // file_service
} // mega
