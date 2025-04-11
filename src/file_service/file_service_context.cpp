#include <mega/common/client.h>
#include <mega/common/lock.h>
#include <mega/common/node_info.h>
#include <mega/common/scoped_query.h>
#include <mega/common/transaction.h>
#include <mega/file_service/database_builder.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_info_context_badge.h>
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

template<typename T>
auto FileServiceContext::getFromIndex(FileID id, FromFileIDMap<std::weak_ptr<T>>& map)
    -> std::shared_ptr<T>
{
    SharedLock guard(mLock);

    if (auto entry = map.find(id); entry != map.end())
        return entry->second.lock();

    return nullptr;
}

auto FileServiceContext::infoFromDatabase(FileID id, bool open)
    -> std::pair<FileInfoContextPtr, FileAccessPtr>
{
    UniqueLock guardContexts(mLock);
    UniqueLock guardDatabase(mDatabase);

    if (auto result = infoFromIndex(id, open); result.first)
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

    auto handle = NodeHandle();

    if (!query.field("handle").null())
        handle = query.field("handle").get<NodeHandle>();

    id = query.field("id").get<FileID>();

    auto file = mStorage.getFile(id);
    auto info = std::make_shared<FileInfoContext>(mActivities.begin(), *file, handle, id, *this);

    mInfoContexts.emplace(id, info);

    if (!open)
        file.reset();

    return std::make_pair(std::move(info), std::move(file));
}

auto FileServiceContext::infoFromIndex(FileID id, bool open)
    -> std::pair<FileInfoContextPtr, FileAccessPtr>
{
    auto info = getFromIndex(id, mInfoContexts);
    auto file = open ? mStorage.getFile(id) : nullptr;

    return std::make_pair(std::move(info), std::move(file));
}

auto FileServiceContext::info(FileID id, bool open) -> std::pair<FileInfoContextPtr, FileAccessPtr>
{
    if (auto result = infoFromIndex(id, open); result.first)
        return result;

    return infoFromDatabase(id, open);
}

auto FileServiceContext::openFromCloud(FileID id) -> FileServiceResultOr<FileContextPtr>
{
    if (synthetic(id))
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    auto node = mClient.get(id.toHandle());

    if (!node)
    {
        if (node.error() == API_ENOENT)
            return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

        return unexpected(FILE_SERVICE_UNEXPECTED);
    }

    if (node->mIsDirectory)
        return unexpected(FILE_SERVICE_FILE_IS_A_DIRECTORY);

    UniqueLock guardContexts(mLock);
    UniqueLock guardDatabase(mDatabase);

    if (auto context = openFromIndex(id))
        return context;

    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mAddFile);

    query.param(":handle").set(node->mHandle);
    query.param(":id").set(id);

    query.execute();

    auto file = mStorage.addFile(id);

    transaction.commit();

    auto info =
        std::make_shared<FileInfoContext>(mActivities.begin(), *file, node->mHandle, id, *this);

    mInfoContexts.emplace(id, info);

    auto context =
        std::make_shared<FileContext>(mActivities.begin(), std::move(file), std::move(info), *this);

    mFileContexts.emplace(id, context);

    return context;
}

auto FileServiceContext::openFromDatabase(FileID id) -> FileServiceResultOr<FileContextPtr>
{
    auto [info, file] = this->info(id, true);

    if (!info)
        return openFromCloud(id);

    UniqueLock guard(mLock);

    if (auto context = openFromIndex(info->id()))
        return context;

    auto context =
        std::make_shared<FileContext>(mActivities.begin(), std::move(file), std::move(info), *this);

    mFileContexts.emplace(id, context);

    return context;
}

auto FileServiceContext::openFromIndex(FileID id) -> FileContextPtr
{
    return getFromIndex(id, mFileContexts);
}

template<typename T>
void FileServiceContext::removeFromIndex(FileID id, FromFileIDMap<T>& map)
{
    UniqueLock guard(mLock);

    if (auto entry = map.find(id); entry != map.end() && entry->second.expired())
        map.erase(id);
}

FileServiceContext::FileServiceContext(Client& client):
    mClient(client),
    mStorage(mClient),
    mDatabase(createDatabase(mStorage.databasePath())),
    mQueries(mDatabase),
    mFileContexts(),
    mInfoContexts(),
    mLock(),
    mActivities()
{}

FileServiceContext::~FileServiceContext() = default;

auto FileServiceContext::info(FileID id) -> FileServiceResultOr<FileInfo>
try
{
    if (auto [context, _] = info(id, false); context)
        return FileInfo({}, std::move(context));

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
    if (auto context = openFromIndex(id))
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

void FileServiceContext::removeFromIndex(FileContextBadge, FileID id)
{
    removeFromIndex(id, mFileContexts);
}

void FileServiceContext::removeFromIndex(FileInfoContextBadge, FileID id)
{
    removeFromIndex(id, mInfoContexts);
}

Database createDatabase(const LocalPath& databasePath)
{
    Database database(logger(), databasePath);

    DatabaseBuilder(database).build();

    return database;
}

} // file_service
} // mega
