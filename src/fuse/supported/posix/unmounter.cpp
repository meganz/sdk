#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/platform/unmounter.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

MountResult Unmounter::unmount(Mount&,
                               const std::string& path,
                               bool abort)
{
    return platform::unmount(path, abort);
}

} // platform
} // fuse
} // mega

