#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/platform/directory_context.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

void DirectoryContext::populate() const
{
    std::lock_guard<std::mutex> guard(mLock);

    // Have already retrieved this directory's children.
    if (mPopulated)
        return;

    // Who are this directory's children?
    mChildren = mDirectory->children();

    // Only non-root directories report uplinks.
    if (!mIsRoot)
    {
        // Add link to self.
        mChildrenByName.emplace_back(".", mChildren.size());
        mChildren.emplace_back(mDirectory);

        // Add link to parent.
        mChildrenByName.emplace_back("..", mChildren.size());
        mChildren.emplace_back(mDirectory->parent());
    }

    // Remember where legitimate children begin.
    mOffset = mChildrenByName.size();

    // Add mappings for legitimate children.
    for (auto m = 0u; m < mChildren.size() - mOffset; ++m)
        mChildrenByName.emplace_back(mChildren[m]->name(), m);

    // Sort children.
    auto less = [](const NameIndexPair& lhs, const NameIndexPair& rhs) {
        return lhs.first < rhs.first;
    }; // less

    std::sort(mChildrenByName.begin() + mOffset,
              mChildrenByName.end(),
              std::move(less));

    // Children have been retrieved.
    mPopulated = true;
}

DirectoryContext::DirectoryContext(DirectoryInodeRef directory,
                                   fuse::Mount& mount,
                                   bool isRoot)
  : Context(mount)
  , mChildren()
  , mChildrenByName()
  , mDirectory(std::move(directory))
  , mLock()
  , mOffset(0)
  , mIsRoot(isRoot)
  , mPopulated(false)
{
    FUSEDebugF("Directory Context %s created",
               toString(mDirectory->id()).c_str());
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

InodeRef DirectoryContext::get(const std::string& name) const
{
    return mDirectory->get(name);
}

void DirectoryContext::get(const std::string& marker,
                           PVOID buffer,
                           ULONG length,
                           const Mount& mount,
                           ULONG& numWritten) const
{
    // Populate children if necessary.
    populate();

    // Assume that we're listing all children.
    auto i = mChildrenByName.begin();
    auto j = i;
    auto k = mChildrenByName.end();

    // Caller's continuing a previous listing.
    if (!marker.empty())
    {
        auto less = [](const std::string& lhs, const NameIndexPair& rhs) {
            return lhs < rhs.first;
        }; // less

        j = std::upper_bound(i, k, marker, std::move(less));
    }

    // Temporary storage.
    std::vector<std::uint8_t> storage;

    // Populate buffer.
    for ( ; j != k; ++j)
    {
        // Get a reference to the current child.
        auto& child = mChildren[j->second];

        // Compute the child's position in the list of entries.
        auto index = static_cast<std::size_t>(j - i);

        // Child's been removed.
        if (!child || child->removed())
        {
            // This directory or its parent no longer exists.
            if (index < mOffset)
            {
                numWritten = 0;
                break;
            }

            // Check the next child.
            continue;
        }

        // Latch this child's description.
        auto info = child->info();

        // Child no longer exists below this directory.
        if (index >= mOffset && info.mParentID != mDirectory->id())
            continue;

        // Try and add this child's description to the buffer.
        if (!FspFileSystemAddDirInfo(translate(storage, mount, j->first, info),
                                     buffer,
                                     length,
                                     &numWritten))
            return;
    }

    // No further children to describe.
    FspFileSystemAddDirInfo(nullptr, buffer, length, &numWritten);
}

InodeRef DirectoryContext::inode() const
{
    return mDirectory;
}

} // platform
} // fuse
} // mega

