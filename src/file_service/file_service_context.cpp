#include <mega/file_service/database_builder.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/logging.h>

namespace mega
{
namespace file_service
{

using namespace common;

static Database createDatabase(const LocalPath& databasePath);

static const std::string kName = "FileServiceContext";

FileServiceContext::FileServiceContext(Client& client):
    DestructionLogger(kName),
    mClient(client),
    mStorage(mClient),
    mDatabase(createDatabase(mStorage.databasePath())),
    mConstructionLogger(kName)
{}

Database createDatabase(const LocalPath& databasePath)
{
    Database database(logger(), databasePath);

    DatabaseBuilder(database).build();

    return database;
}

} // file_service
} // mega
