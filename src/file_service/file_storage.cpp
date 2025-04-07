#include <mega/common/client.h>
#include <mega/file_service/file_storage.h>
#include <mega/file_service/logging.h>

#include <megafs.h>

namespace mega
{
namespace file_service
{

using namespace common;

static const std::string kName = "FileStorage";

FileStorage::FileStorage(const Client& client):
    DestructionLogger(kName),
    mFilesystem(std::make_unique<FSACCESS_CLASS>()),
    mStorageDirectory(*mFilesystem, logger(), "file-service", client.dbRootPath()),
    mUserStorageDirectory(*mFilesystem, logger(), client.sessionID(), mStorageDirectory),
    mConstructionLogger(kName)
{}

FileStorage::~FileStorage() = default;

auto FileStorage::databasePath() const -> LocalPath
{
    static const auto name = LocalPath::fromRelativePath("metadata");

    auto path = mUserStorageDirectory.path();

    path.appendWithSeparator(name, true);

    return path;
}

auto FileStorage::storageDirectory() const -> const LocalPath&
{
    return mStorageDirectory;
}

auto FileStorage::userStorageDirectory() const -> const LocalPath&
{
    return mUserStorageDirectory;
}

} // file_service
} // mega
