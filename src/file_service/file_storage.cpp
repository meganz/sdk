#include <mega/common/client.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_storage.h>
#include <mega/file_service/logging.h>

#include <megafs.h>

namespace mega
{
namespace file_service
{

using namespace common;

static const std::string kName = "FileStorage";

auto FileStorage::openFile(FileID id, bool mustCreate) -> FileAccessPtr
{
    auto file = mFilesystem->newfileaccess(false);
    auto path = userFilePath(id);

    if (file->isfile(path) == !mustCreate && file->fopen(path, true, true, FSLogging::noLogging))
        return file;

    throw FSErrorF("Couldn't %s file: %s",
                   mustCreate ? "create" : "open",
                   path.toPath(false).c_str());
}

auto FileStorage::userFilePath(FileID id) const -> LocalPath
{
    auto name = LocalPath::fromRelativePath(toString(id));
    auto path = userStorageDirectory();

    path.appendWithSeparator(name, false);

    return path;
}

FileStorage::FileStorage(const Client& client):
    DestructionLogger(kName),
    mFilesystem(std::make_unique<FSACCESS_CLASS>()),
    mStorageDirectory(*mFilesystem, logger(), "file-service", client.dbRootPath()),
    mUserStorageDirectory(*mFilesystem, logger(), client.sessionID(), mStorageDirectory),
    mConstructionLogger(kName)
{}

FileStorage::~FileStorage() = default;

auto FileStorage::addFile(FileID id) -> FileAccessPtr
{
    return openFile(id, true);
}

auto FileStorage::databasePath() const -> LocalPath
{
    static const auto name = LocalPath::fromRelativePath("metadata");

    auto path = mUserStorageDirectory.path();

    path.appendWithSeparator(name, true);

    return path;
}

auto FileStorage::getFile(FileID id) -> FileAccessPtr
{
    return openFile(id, false);
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
