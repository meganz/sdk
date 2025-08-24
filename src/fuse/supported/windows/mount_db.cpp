#include <sstream>

#include <mega/fuse/common/client.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/security_identifier.h>
#include <mega/fuse/platform/service_context.h>

#include <mega/filesystem.h>

namespace mega
{
namespace fuse
{
namespace platform
{

using namespace common;

MountResult MountDB::check(const Client& client,
                           const MountInfo& info) const
{
    // Convenience.
    auto& name = info.name();
    auto& path = info.mPath;

    // Check if WinFSP is actually available.
    if (FspLoad(nullptr) != STATUS_SUCCESS)
        return MOUNT_BACKEND_UNAVAILABLE;

    // Make sure the mount's name is within limits.
    if (name.size() > MaxMountNameLength)
    {
        FUSEErrorF("Name too long: %s (%lu > %lu)", name.c_str(), name.size(), MaxMountNameLength);

        return MOUNT_NAME_TOO_LONG;
    }

    // Make sure the mount's name contains no invalid characters
    // Refer https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    constexpr const char* invalidChars = "<>:\"/\\|?*";
    if (name.find_first_of(invalidChars) != std::string::npos)
    {
        FUSEErrorF("Name contains invalid character(s): %s", name.c_str());

        return MOUNT_NAME_INVALID_CHAR;
    }

    // An unspecified path signals we should assign a drive letter.
    if (path.empty())
        return MOUNT_SUCCESS;

    // Make sure nothing exists at the path.
    auto fileAccess = client.fsAccess().newfileaccess(true);

    // Check if something already exists at the path.
    fileAccess->fopen(path, FSLogging::noLogging);

    // Convenience.
    auto result = fileAccess->errorcode;

    // Something already exists at the path.
    if (result == ERROR_SUCCESS)
    {
        FUSEErrorF("Local path is already occupied: %s",
                   path.toPath(false).c_str());

        return MOUNT_LOCAL_EXISTS;
    }

    // Some parent doesn't exist.
    if (result == ERROR_PATH_NOT_FOUND)
    {
        FUSEErrorF("Local path doesn't exist: %s",
                   path.toPath(false).c_str());

        return MOUNT_LOCAL_UNKNOWN;
    }

    // Nothing exists at the path. We're all good.
    if (result == ERROR_FILE_NOT_FOUND)
        return MOUNT_SUCCESS;

    // Couldn't determine whether anything exists at the path.
    FUSEErrorF("Couldn't determine status of path: %s: %d",
               path.toPath(false).c_str(),
               result);

    return MOUNT_UNEXPECTED;
}

MountDB::MountDB(ServiceContext& context)
  : fuse::MountDB(context)
  , mReadOnlySecurityDescriptor(readOnlySecurityDescriptor())
  , mReadWriteSecurityDescriptor(readWriteSecurityDescriptor())
{
    FUSEDebug1("Mount DB constructed");
}

void MountDB::notifyFileExplorerSetter()
{
    auto getPrefixes = [this]()
    {
        auto mounts = get(true);
        std::vector<std::wstring> prefixes;
        std::transform(mounts.begin(),
                       mounts.end(),
                       std::back_inserter(prefixes),
                       [](const MountInfo& mount)
                       {
                           return mount.mPath.asPlatformEncoded(true);
                       });
        return prefixes;
    };
    if (fileExplorerView() != FILE_EXPLORER_VIEW_NONE)
        mFileExplorerSetter.notify(getPrefixes);
}

} // platform
} // fuse
} // mega

