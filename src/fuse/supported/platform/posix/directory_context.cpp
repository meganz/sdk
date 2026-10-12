#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/inode.h>
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

DirectoryContext::DirectoryContext(DirectoryInodeRef directory, fuse::Mount& mount):
    Context(mount),
    mChildren(),
    mDirectory(std::move(directory)),
    mLock(),
    mParent(mDirectory->parent()),
    mPopulated(false)
{
    FUSEDebugF("Directory Context %s created", toString(mDirectory->id()).c_str());

    // Directory has no parent but one must be reported.
    if (!mParent)
        mParent = mDirectory;
}

DirectoryContext::~DirectoryContext()
{
    FUSEDebugF("Directory Context %s destroyed", toString(mDirectory->id()).c_str());
}

DirectoryContext* DirectoryContext::directory()
{
    return this;
}

InodeRef DirectoryContext::get(std::size_t index) const
{
    assert(index < size());

    // Populate entries if necessary.
    populate();

    // Caller's interested in one of our children.
    if (index > 1)
        return mChildren[index - 2];

    // Caller's interested in our parent.
    if (index > 0)
        return mParent;

    // Caller's interested in us.
    return mDirectory;
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
