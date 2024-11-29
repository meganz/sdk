#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/node_info.h>

namespace mega
{
namespace fuse
{

InodeInfo::InodeInfo(NodeInfo info)
  : mID(InodeID(info.mHandle))
  , mIsDirectory(info.mIsDirectory)
  , mModified(info.mModified)
  , mName(std::move(info.mName))
  , mParentID(InodeID(info.mParentHandle))
  , mPermissions(info.mPermissions)
  , mSize(info.mSize)
{
}

InodeInfo::InodeInfo(InodeID id, NodeInfo info)
  : mID(id)
  , mIsDirectory(info.mIsDirectory)
  , mModified(info.mModified)
  , mName(std::move(info.mName))
  , mParentID(InodeID(info.mParentHandle))
  , mPermissions(info.mPermissions)
  , mSize(info.mSize)
{
}

} // fuse
} // mega

