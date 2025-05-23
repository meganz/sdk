#include <mega/common/client.h>
#include <mega/common/node_info.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_storage.h>
#include <mega/file_service/logging.h>

#include <megafs.h>

namespace mega
{
namespace file_service
{

using namespace common;

FileAccessPtr FileStorage::openFile(const LocalPath& path, bool mustCreate)
{
    auto file = mFilesystem->newfileaccess(false);

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

FileAccessPtr FileStorage::addFile(const NodeInfo& info)
{
    // Translate node handle to a FileID.
    auto id = FileID::from(info.mHandle);

    // Compute the file's path.
    auto path = userFilePath(id);

    // Create the file.
    auto file = openFile(path, true);

    // Make sure the file's correctly sized.
    if (file->ftruncate(info.mSize) && file->fstat())
        return file;

    // Try and remove the file.
    mFilesystem->unlinklocal(path);

    // Let our caller know we couldn't create the file.
    throw FSErrorF("Couldn't set file size: %s", path.toPath(false).c_str());
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
    return openFile(userFilePath(id), false);
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
