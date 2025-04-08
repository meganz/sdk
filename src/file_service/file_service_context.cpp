#include <mega/common/lock.h>
#include <mega/common/scoped_query.h>
#include <mega/common/transaction.h>
#include <mega/file_service/database_builder.h>
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

static const std::string kName = "FileServiceContext";

auto FileServiceContext::infoFromDatabase(FileID id) -> FileInfoContextPtr
{
    UniqueLock<SharedMutex> guardContexts(mLock);

    if (auto context = infoFromIndex(id))
        return context;

    UniqueLock<Database> guardDatabase(mDatabase);

    auto transaction = mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetFile);

    query.param(":handle").set(nullptr);
    query.param(":id").set(id);

    if (!synthetic(id))
        query.param(":handle").set(id.toHandle());

    query.execute();

    if (!query)
        return nullptr;

    auto handle = NodeHandle();

    if (!query.field("handle").null())
        handle = query.field("handle").get<NodeHandle>();

    id = query.field("id").get<FileID>();

    auto file = mStorage.getFile(id);
    auto info = std::make_shared<FileInfoContext>(mActivities.begin(), *file, handle, id, *this);

    mInfoContexts.emplace(id, info);

    return info;
}

auto FileServiceContext::infoFromIndex(FileID id) -> FileInfoContextPtr
{
    SharedLock<SharedMutex> guard(mLock);

    if (auto entry = mInfoContexts.find(id); entry != mInfoContexts.end())
        return entry->second.lock();

    return nullptr;
}

template<typename T>
auto FileServiceContext::removeFromIndex(FileID id, FromFileIDMap<T>& map) -> void
{
    UniqueLock<SharedMutex> guard(mLock);

    if (auto entry = map.find(id); entry != map.end() && entry->second.expired())
        map.erase(id);
}

FileServiceContext::FileServiceContext(Client& client):
    DestructionLogger(kName),
    mClient(client),
    mStorage(mClient),
    mDatabase(createDatabase(mStorage.databasePath())),
    mQueries(mDatabase),
    mInfoContexts(),
    mLock(),
    mActivities(),
    mConstructionLogger(kName)
{}

FileServiceContext::~FileServiceContext() = default;

auto FileServiceContext::info(FileID id) -> FileServiceResultOr<FileInfo>
try
{
    if (auto context = infoFromIndex(id); context)
        return FileInfo({}, std::move(context));

    if (auto context = infoFromDatabase(id); context)
        return FileInfo({}, std::move(context));

    return unexpected(FILE_SERVICE_UNKNOWN_FILE);
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to get file information: %s: %s", toString(id).c_str(), exception.what());

    return unexpected(FILE_SERVICE_UNEXPECTED);
}

auto FileServiceContext::removeFromIndex(FileInfoContextBadge, FileID id) -> void
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
