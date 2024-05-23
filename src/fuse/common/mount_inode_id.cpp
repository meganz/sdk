#include <cassert>

#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/common/inode_id.h>

#include <mega/utils.h>

namespace mega
{
namespace fuse
{

MountInodeID::MountInodeID(InodeID id)
  : mValue(id.get())
{
    assert(id);
}

MountInodeID::MountInodeID(std::uint64_t value)
  : mValue(value)
{
    assert(mValue);
}

bool MountInodeID::operator==(const MountInodeID& rhs) const
{
    return mValue == rhs.mValue;
}

bool MountInodeID::operator<(const MountInodeID& rhs) const
{
    return mValue < rhs.mValue;
}

bool MountInodeID::operator!=(const MountInodeID& rhs) const
{
    return mValue != rhs.mValue;
}

std::uint64_t MountInodeID::get() const
{
    return mValue;
}

std::string toString(MountInodeID id)
{
    return toHandle(id.get());
}

} // fuse
} // mega

