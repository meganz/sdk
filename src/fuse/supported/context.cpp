#include <mega/common/badge.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/context.h>
#include <mega/fuse/platform/mount.h>

namespace mega
{
namespace fuse
{
namespace platform
{

Context::Context(fuse::Mount& mount)
  : mMount(&mount)
{
    mMount->contextAdded(ContextBadge(), *this);
}

Context::~Context()
{
    mMount->contextRemoved(ContextBadge(), *this);
}

DirectoryContext* Context::directory()
{
    return nullptr;
}

FileContext* Context::file()
{
    return nullptr;
}

InodeInfo Context::info() const
{
    return inode()->info();
}

fuse::Mount& Context::mount() const
{
    assert(mMount);

    return *mMount;
}

} // platform
} // fuse
} // mega

