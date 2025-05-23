#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/directory.h>
#include <mega/common/node_info_forward.h>
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
    FileAccessPtr openFile(const LocalPath& path, bool mustCreate);

    LocalPath userFilePath(FileID id) const;

    FileSystemAccessPtr mFilesystem;
    common::Directory mStorageDirectory;
    common::Directory mUserStorageDirectory;

public:
    explicit FileStorage(const common::Client& client);

    ~FileStorage();

    FileAccessPtr addFile(const common::NodeInfo& info);

    LocalPath databasePath() const;

    FileAccessPtr getFile(FileID id);

    const LocalPath& storageDirectory() const;

    const LocalPath& userStorageDirectory() const;
}; // FileStorage

} // file_service
} // mega
