#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/directory.h>
#include <mega/file_service/construction_logger.h>
#include <mega/file_service/destruction_logger.h>
#include <mega/types.h>

namespace mega
{
namespace file_service
{

class FileStorage: DestructionLogger
{
    auto userFilePath(FileID id) const -> LocalPath;

    FileSystemAccessPtr mFilesystem;
    common::Directory mStorageDirectory;
    common::Directory mUserStorageDirectory;
    ConstructionLogger mConstructionLogger;

public:
    explicit FileStorage(const common::Client& client);

    ~FileStorage();

    auto databasePath() const -> LocalPath;

    auto getFile(FileID id) -> FileAccessPtr;

    auto storageDirectory() const -> const LocalPath&;

    auto userStorageDirectory() const -> const LocalPath&;
}; // FileStorage

} // file_service
} // mega
