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

FileAccessPtr FileStorage::openFile(FileID id, bool mustCreate)
{
    auto file = mFilesystem->newfileaccess(false);
    auto path = userFilePath(id);

    // Vulnerable to TOCTOU race.
    if (file->isfile(path) == !mustCreate && file->fopen(path, true, true, FSLogging::noLogging))
        return file;

    throw FSErrorF("Couldn't %s file: %s",
                   mustCreate ? "create" : "open",
                   path.toPath(false).c_str());
}

LocalPath FileStorage::userFilePath(FileID id) const
{
    auto name = LocalPath::fromRelativePath(toString(id));
    auto path = userStorageDirectory();

    path.appendWithSeparator(name, false);

    return path;
}

FileStorage::FileStorage(const Client& client):
    mFilesystem(std::make_unique<FSACCESS_CLASS>()),
    mStorageDirectory(*mFilesystem, logger(), "file-service", client.dbRootPath()),
    mUserStorageDirectory(*mFilesystem, logger(), client.sessionID(), mStorageDirectory)
{}

FileStorage::~FileStorage() = default;

FileAccessPtr FileStorage::addFile(FileID id)
{
    return openFile(id, true);
}

LocalPath FileStorage::databasePath() const
{
    static const auto name = LocalPath::fromRelativePath("metadata");

    auto path = mUserStorageDirectory.path();

    path.appendWithSeparator(name, true);

    return path;
}

FileAccessPtr FileStorage::getFile(FileID id)
{
    return openFile(id, false);
}

const LocalPath& FileStorage::storageDirectory() const
{
    return mStorageDirectory;
}

const LocalPath& FileStorage::userStorageDirectory() const
{
    return mUserStorageDirectory;
}

} // file_service
} // mega
