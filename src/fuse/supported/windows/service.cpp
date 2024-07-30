#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/service.h>

namespace mega
{
namespace fuse
{

MountResult Service::abort(AbortPredicate)
{
    return MOUNT_SUCCESS;
}

} // fuse
} // mega

