#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

PathVector filesystems(FilesystemPredicate predicate)
{
    // How many filesystems are mounted?
    auto count = getfsstat(nullptr, 0, MNT_NOWAIT);

    // Couldn't determine how many filesystems were mounted.
    if (count < 0)
    {
        FUSEWarningF("Couldn't retrieve number of mounted filesystems: %s",
                     std::strerror(errno));

        return PathVector();
    }

    // Will contain a description of each mounted filesystem.
    std::vector<struct statfs> filesystems;

    // Allocate enough memory for our descriptions.
    filesystems.resize(static_cast<std::size_t>(count));

    // Convenience.
    count *= sizeof(struct statfs);

    // Retrieve a description of each mounted filesystem.
    auto result = getfsstat(&filesystems[0], count, MNT_NOWAIT);

    // Couldn't retrieve filesystem descriptions.
    if (result < 0)
    {
        FUSEWarningF("Couldn't retrieve filesystem descriptions: %s",
                     std::strerror(errno));

        return PathVector();
    }

    PathVector matches;

    // Iterate over filesystems.
    for (const auto& filesystem : filesystems)
    {
        // Latch each filesystem's path and type.
        std::string path = filesystem.f_mntonname;
        std::string type = filesystem.f_fstypename;

        // Collect path if our predicate is satisfied.
        if (!predicate || predicate(path, type))
            matches.emplace_back(std::move(path));
    }

    // Pass matches to our caller.
    return matches;
}

MountResult unmount(const std::string& path, bool)
{
    if (!::unmount(path.c_str(), MNT_FORCE))
        return MOUNT_SUCCESS;

    if (errno == EBUSY)
        return MOUNT_BUSY;

    return MOUNT_UNEXPECTED;
}

} // platform
} // fuse
} // mega

