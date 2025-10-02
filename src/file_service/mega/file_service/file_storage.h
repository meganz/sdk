#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/directory.h>
#include <mega/common/node_info_forward.h>
#include <mega/common/platform/folder_locker.h>
#include <mega/types.h>

#include <optional>

namespace mega
{
namespace file_service
{

class FileStorage
{
    // This function exists solely to reduce duplication.
    //
    // It is called by both addFile(...) and getFile(...).
    //
    // mustCreate specifies whether we need to create a new file for id.
    //
    // It's an error if mustCreate is true and a file for id already exists.
    // It's an error if mustCreate is false but no file for id exists.
    FileAccessPtr openFile(const LocalPath& path, bool mustCreate);

    // How we interact with the host filesystem.
    FileSystemAccessPtr mFilesystem;

    // Where the service is storing its metadata.
    common::Directory mStorageDirectory;

    // Where the service is storing this user's metadata.
    common::Directory mUserStorageDirectory;

    // Where the service is storing this user's cached files
    common::Directory mUserCacheDirectory;

    common::platform::FolderLocker mFolderLocker;

public:
    explicit FileStorage(const common::Client& client);

    ~FileStorage();

    // Add a new file to our storage area.
    FileAccessPtr addFile(FileID id);

    // Where is the service storing this user's database?
    LocalPath databasePath() const;

    // Get a file from our storage area.
    FileAccessPtr getFile(FileID id);

    // Remove a file from our storage area.
    void removeFile(FileID id);

    // Find out where the service is storing a particular file.
    LocalPath userFilePath(FileID id) const;
}; // FileStorage

} // file_service
} // mega
