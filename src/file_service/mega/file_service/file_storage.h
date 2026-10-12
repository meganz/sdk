#pragma once

#include <mega/common/directory.h>
#include <mega/common/node_info_forward.h>
#include <mega/common/platform/folder_locker.h>
#include <mega/types.h>

namespace mega
{
namespace file_service
{

struct UserStoragePath
{
    LocalPath dbRoot;
    std::string userName;
};

class FileStorage
{
    // This function exists solely to simplify getFile(...).
    FileAccessPtr openFile(const LocalPath& path);

    // How we interact with the host filesystem.
    FileSystemAccessPtr mFilesystem;

    // Where the service is storing its metadata.
    common::Directory mStorageDirectory;

    // Where the service is storing this user's metadata.
    common::Directory mUserStorageDirectory;

    // Where the service is storing this user's cached files
    common::Directory mUserCacheDirectory;

    // On Windows, prevent others, especially file explorer, from opening files under the folder,
    // generating thumbnail while we're running. We have seen we're blocked to open files forever
    // due to this.
    common::platform::FolderLocker mFolderLocker;

public:
    explicit FileStorage(const UserStoragePath& userStoragePath);

    ~FileStorage();

    // Where is the service storing this user's database?
    LocalPath databasePath() const;

    // Get a file from our storage area.
    //
    // If force is true, the file will always be resized to expectedSize
    // regardless of its current size on disk.
    FileAccessPtr getFile(FileID id, std::uint64_t expectedSize, const bool force);

    // Remove a file from our storage area.
    bool removeFile(FileID id, std::nothrow_t);
    void removeFile(FileID id);

    // Find out where the service is storing a particular file.
    LocalPath userFilePath(FileID id) const;

    // Find out where the service is storing this user's data.
    const LocalPath& userStoragePath() const;
}; // FileStorage

} // file_service
} // mega
