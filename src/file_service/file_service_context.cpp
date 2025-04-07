#include <mega/common/lock.h>
#include <mega/file_service/database_builder.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_info_context_badge.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/logging.h>

namespace mega
{
namespace file_service
{

using namespace common;

static Database createDatabase(const LocalPath& databasePath);

static const std::string kName = "FileServiceContext";

template<typename T>
auto FileServiceContext::remove(FileID id, FromFileIDMap<T>& map) -> void
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

auto FileServiceContext::remove(FileInfoContextBadge, FileID id) -> void
{
    remove(id, mInfoContexts);
}

Database createDatabase(const LocalPath& databasePath)
{
    Database database(logger(), databasePath);

    DatabaseBuilder(database).build();

    return database;
}

} // file_service
} // mega
