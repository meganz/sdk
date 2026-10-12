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

static constexpr const char* USER_STORAGE_VER = "_v1";

FileAccessPtr FileStorage::openFile(const LocalPath& path)
{
    // So we can access the filesystem.
    auto file = mFilesystem->newfileaccess(false);

    // Sanity: We should always be able to create a file access instance.
    assert(file);

    // Should never happen but hey, just in case.
    if (file == nullptr)
        throw FSError1("Couldn't create file access instance");

    // Can't open the file for reading and writing.
    if (!file->fopen(path, OPEN_RDWR, FSLogging::logOnError))
        throw FSErrorF("Couldn't open file: %s", path.toPath(false).c_str());

    // Couldn't get file information.
    if (!file->fstat())
        throw FSErrorF("Couldn't get file information: %s", path.toPath(false).c_str());

    // Mark the file as a sparse file if supported.
    file->setSparse();

    // Return the file to our caller.
    return file;
}

FileStorage::FileStorage(const UserStoragePath& userStoragePath):
    mFilesystem(std::make_unique<FSACCESS_CLASS>()),
    mStorageDirectory(*mFilesystem, logger(), "file-service", userStoragePath.dbRoot),
    mUserStorageDirectory(*mFilesystem,
                          logger(),
                          userStoragePath.userName + USER_STORAGE_VER,
                          mStorageDirectory),
    mUserCacheDirectory(*mFilesystem, logger(), "cache", mUserStorageDirectory),
    mFolderLocker(mUserCacheDirectory.path().asPlatformEncoded(true))
{
    // Storage cache file names are now encoded as hex
    // strings instead of Base64. We start to use a new user storage directory and remove the old
    // user storage directory. This is safe because, at the time of this change, only the read-only
    // streaming feature uses the File Service. The only impact is that cached files are removed and
    // will be fetched from the cloud again when accessed.
    LocalPath oldUserStorageDirectory(mStorageDirectory);
    oldUserStorageDirectory.appendWithSeparator(
        LocalPath::fromRelativePath(userStoragePath.userName),
        true);
    FSACCESS_CLASS::emptydirlocal(oldUserStorageDirectory);
    mFilesystem->rmdirlocal(oldUserStorageDirectory);
}

FileStorage::~FileStorage() = default;

LocalPath FileStorage::databasePath() const
{
    const auto name = LocalPath::fromRelativePath("metadata");

    auto path = mUserStorageDirectory.path();

    path.appendWithSeparator(name, true);

    return path;
}

FileAccessPtr FileStorage::getFile(FileID id, std::uint64_t expectedSize, const bool force)
{
    // Compute the file's path.
    const auto path = userFilePath(id);

    // Open (or create) this file's cache file.
    auto file = openFile(path);

    // Convenience.
    const auto currentSize = static_cast<std::uint64_t>(file->size);

    // No need to truncate as the file's already of the expected size.
    if (!force && currentSize == expectedSize)
        return file;

    // No need to truncate as the file's smaller than our expected size.
    if (!force && currentSize < expectedSize)
        return file;

    // File's larger than our expected size.
    if (!file->ftruncate(static_cast<m_off_t>(expectedSize)))
        throw FSErrorF("Couldnt truncate file: %s", path.toPath(false).c_str());

    // Update the file's cached size.
    file->size = static_cast<m_off_t>(expectedSize);

    // Return the file to our caller.
    return file;
}

bool FileStorage::removeFile(FileID id, std::nothrow_t)
{
    // Compute the file's path.
    auto path = userFilePath(id);

    // File was removed from storage.
    if (mFilesystem->unlinklocal(path))
        return true;

    // Couldn't remove the file from storage.
    FSWarningF("Couldn't remove file: %s", path.toPath(false).c_str());

    return false;
}

void FileStorage::removeFile(FileID id)
{
    // Compute the file's path.
    auto path = userFilePath(id);

    // File was removed from storage.
    if (mFilesystem->unlinklocal(path))
        return;

    // Couldn't remove the file from storage.
    throw FSErrorF("Couldn't remove file: %s", path.toPath(false).c_str());
}

LocalPath FileStorage::userFilePath(FileID id) const
{
    auto name = LocalPath::fromRelativePath(toString(id));
    auto path = mUserCacheDirectory.path();

    path.appendWithSeparator(name, false);

    return path;
}

const LocalPath& FileStorage::userStoragePath() const
{
    return mUserStorageDirectory.path();
}

} // file_service
} // mega
