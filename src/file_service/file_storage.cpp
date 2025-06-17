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

// Convenience.
class ScopedFileRemover
{
    FileSystemAccess& mFilesystem;
    const LocalPath* mPath;

public:
    ScopedFileRemover(FileSystemAccess& filesystem, const LocalPath& path);

    ~ScopedFileRemover();

    void release();
}; // ScopedFileRemover

FileAccessPtr FileStorage::openFile(const LocalPath& path, bool mustCreate)
{
    auto file = mFilesystem->newfileaccess(false);

    // Vulnerable to TOCTOU race.
    if (file->isfile(path) != !mustCreate || !file->fopen(path, true, true, FSLogging::noLogging))
        throw FSErrorF("Couldn't %s file: %s",
                       mustCreate ? "create" : "open",
                       path.toPath(false).c_str());

    // Try and mark the file as a sparse file.
    if (!file->setSparse())
        FSWarningF("Couldn't mark file %s as a sparse file", path.toPath(false).c_str());

    return file;
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

    // Remove file if we escape due to an exception.
    ScopedFileRemover remover(*mFilesystem, path);

    // Convenience.
    auto failure = [&path](const char* message)
    {
        return FSErrorF("%s: %s", message, path.toPath(false).c_str());
    }; // failure

    // Couldn't set the file's size.
    if (!file->ftruncate(info.mSize))
        throw failure("Couldn't set file size");

    // Couldn't retrieve the file's attributes.
    if (!file->fstat())
        throw failure("Couldn't retrieve file attributes");

    // Everything's okay: don't remove the file.
    remover.release();

    // Return the file to our caller.
    return file;
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

const LocalPath& FileStorage::storageDirectory() const
{
    return mStorageDirectory;
}

LocalPath FileStorage::userFilePath(FileID id) const
{
    auto name = LocalPath::fromRelativePath(toString(id));
    auto path = userStorageDirectory();

    path.appendWithSeparator(name, false);

    return path;
}

const LocalPath& FileStorage::userStorageDirectory() const
{
    return mUserStorageDirectory;
}

ScopedFileRemover::ScopedFileRemover(FileSystemAccess& filesystem, const LocalPath& path):
    mFilesystem(filesystem),
    mPath(&path)
{}

ScopedFileRemover::~ScopedFileRemover()
{
    // No path to remove.
    if (!mPath)
        return;

    // File's been removed.
    if (mFilesystem.unlinklocal(*mPath))
        return;

    // Couldn't remove the file.
    FSWarningF("Couldn't remove file: %s", mPath->toPath(false).c_str());
}

void ScopedFileRemover::release()
{
    mPath = nullptr;
}

} // file_service
} // mega
