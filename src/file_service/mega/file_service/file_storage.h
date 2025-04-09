#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/directory.h>
#include <mega/types.h>

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
    auto openFile(FileID id, bool mustCreate) -> FileAccessPtr;

    auto userFilePath(FileID id) const -> LocalPath;

    FileSystemAccessPtr mFilesystem;
    common::Directory mStorageDirectory;
    common::Directory mUserStorageDirectory;

public:
    explicit FileStorage(const common::Client& client);

    ~FileStorage();

    auto addFile(FileID id) -> FileAccessPtr;

    auto databasePath() const -> LocalPath;

    auto getFile(FileID id) -> FileAccessPtr;

    auto storageDirectory() const -> const LocalPath&;

    auto userStorageDirectory() const -> const LocalPath&;
}; // FileStorage

} // file_service
} // mega
