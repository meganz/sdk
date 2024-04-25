#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/directory_context.h>

namespace mega
{
namespace fuse
{
namespace platform
{

void DirectoryContext::populate() const
{
    std::lock_guard<std::mutex> guard(mLock);

    // Retrieve children if necessary.
    if (!mPopulated)
        mChildren = mDirectory->children();

    // Remember that we've retrieved this directory's children.
    mPopulated = true;
}

DirectoryContext::DirectoryContext(DirectoryInodeRef directory,
                                   fuse::Mount& mount)
  : Context(mount)
  , mChildren()
  , mDirectory(std::move(directory))
  , mLock()
  , mParent(mDirectory->parent())
  , mPopulated(false)
{
    FUSEDebugF("Directory Context %s created",
               toString(mDirectory->id()).c_str());

    // Directory has no parent but one must be reported.
    if (!mParent)
        mParent = mDirectory;
}

DirectoryContext::~DirectoryContext()
{
    FUSEDebugF("Directory Context %s destroyed",
               toString(mDirectory->id()).c_str());
}

DirectoryContext* DirectoryContext::directory()
{
    return this;
}

InodeInfo DirectoryContext::get(std::size_t index) const
{
    assert(index < size());

    // Populate entries if necessary.
    populate();

    // Assume the caller's interested in this directory.
    InodeRef child = mDirectory;

    if (index >= 2)
        child = mChildren[index - 2];
    else if (index)
        child = mParent;

    // Child no longer exists.
    if (!child || child->removed())
        return InodeInfo();

    // Get our hands on the child's description.
    auto info = child->info();

    // Child's no longer below this directory.
    if (index >= 2 && info.mParentID != mDirectory->id())
        return InodeInfo();

    // Tweak filename for symbolic links.
    if (index < 2)
        info.mName.assign(index + 1, '.');

    // Return description to caller.
    return info;
}

InodeRef DirectoryContext::inode() const
{
    return mDirectory;
}

std::size_t DirectoryContext::size() const
{
    // Populate entries if necessary.
    populate();

    // Two extra for . and ..
    return mChildren.size() + 2;
}

} // platform
} // fuse
} // mega

