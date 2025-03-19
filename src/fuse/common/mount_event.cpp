#include <mega/fuse/common/mount_event.h>

namespace mega
{
namespace fuse
{

bool MountEvent::operator==(const MountEvent& rhs) const
{
    return mResult == rhs.mResult
           && mType == rhs.mType
           && mName == rhs.mName;
}

bool MountEvent::operator!=(const MountEvent& rhs) const
{
    return !(*this == rhs);
}

} // fuse
} // mega
