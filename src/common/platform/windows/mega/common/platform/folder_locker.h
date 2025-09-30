#pragma once

#include <mega/common/platform/windows.h>

#include <string>

namespace mega
{
namespace common
{
namespace platform
{

// Open the path and keep opening exclusively during the lifetime of the object
// Note: If the folder has been opened by others, the exclusive opening fails.
class FolderLocker
{
    HANDLE mHandle{INVALID_HANDLE_VALUE};

public:
    FolderLocker() = default;

    // path is a folder
    FolderLocker(const std::wstring& path);

    FolderLocker& operator=(FolderLocker&& other);

    ~FolderLocker();

    void release();
};

} // platform
} // common
} // mega
