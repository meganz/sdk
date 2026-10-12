#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/mount_inode_id.h>

namespace mega
{
namespace fuse
{

using file_service::FileID;

InodeID::InodeID(std::uint64_t id):
    FileID(id)
{}

InodeID::InodeID(FileID id):
    FileID(id)
{}

InodeID InodeID::from(MountInodeID id)
{
    return InodeID(id.get());
}

} // fuse
} // mega
