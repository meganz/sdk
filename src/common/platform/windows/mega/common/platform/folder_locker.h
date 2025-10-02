#pragma once

#include <mega/common/platform/handle.h>
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
    Handle<DefaultHandleDeleter> mHandle{};

public:
    FolderLocker() = default;

    // path is a folder
    FolderLocker(const std::wstring& path);

    FolderLocker& operator=(FolderLocker&& other);

    ~FolderLocker();

    void reset();
};

} // platform
} // common
} // mega
