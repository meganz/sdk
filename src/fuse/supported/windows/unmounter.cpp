#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/unmounter.h>

namespace mega
{
namespace fuse
{
namespace platform
{

MountResult Unmounter::unmount(Mount& mount, const std::string&, bool)
{
    // Remove the mount from memory.
    return mount.remove();
}

} // platform
} // fuse
} // mega

