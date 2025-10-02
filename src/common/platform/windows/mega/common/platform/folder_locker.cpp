#include "mega/common/platform/folder_locker.h"

#include "mega/logging.h"

namespace mega
{
namespace common
{
namespace platform
{

FolderLocker::FolderLocker(const std::wstring& path)
{
    mHandle.reset(CreateFile(path.c_str(), // folder path
                             GENERIC_READ, // desired access (must request at least read)
                             0, // share mode = 0 (deny all sharing)
                             NULL, // security attributes
                             OPEN_EXISTING, // must exist
                             FILE_FLAG_BACKUP_SEMANTICS, // required for opening directories
                             NULL));

    if (mHandle.get() == INVALID_HANDLE_VALUE)
        LOG_warn << "Exclusive open folder failed " << GetLastError();
    else
        LOG_info << "Exclusive open folder OK";
}

FolderLocker& FolderLocker::operator=(FolderLocker&& other)
{
    mHandle = std::move(other.mHandle);
    return *this;
}

FolderLocker::~FolderLocker()
{
    reset();
}

void FolderLocker::reset()
{
    mHandle.reset();
}

} // platform
} // common
} // mega
