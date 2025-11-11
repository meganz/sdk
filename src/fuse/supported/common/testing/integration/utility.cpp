#include <mega/common/node_info.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/types.h>
#include <mega/utils.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;

NodeHandle id(const NodeInfo& info)
{
    return info.mHandle;
}

InodeID id(const InodeInfo& info)
{
    return info.mID;
}

NodeHandle parentID(const NodeInfo& info)
{
    return info.mParentHandle;
}

InodeID parentID(const InodeInfo& info)
{
    return info.mParentID;
}

std::string toString(NodeHandle handle)
{
    return toNodeHandle(handle);
}

std::uint64_t toUint64(InodeID id)
{
    return id.get();
}

std::uint64_t toUint64(NodeHandle handle)
{
    return handle.as8byte();
}

} // testing
} // fuse
} // mega
