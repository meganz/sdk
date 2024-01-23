#pragma once

#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/context.h>
#include <mega/fuse/platform/directory_context_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class DirectoryContext
  : public Context
{
    // Retrieve this directory's children.
    void populate() const;

    // The directory's (last known) children.
    mutable InodeRefVector mChildren;

    // The directory we're iterating.
    DirectoryInodeRef mDirectory;

    // Serializes access to instance members.
    mutable std::mutex mLock;

    // The parent of the directory we're iterating.
    DirectoryInodeRef mParent;

    // Have we retrieved all of this directory's children?
    mutable bool mPopulated;

public:
    DirectoryContext(DirectoryInodeRef directory,
                     fuse::Mount& mount);

    ~DirectoryContext();

    // Check if this context represents a directory.
    DirectoryContext* directory() override;

    // Retrieve information about a specific directory entry.
    InodeInfo get(std::size_t index) const;

    // What inode does this context represent?
    InodeRef inode() const override;

    // How many entries does this directory contain?
    std::size_t size() const;
}; // DirectoryContext

} // platform
} // fuse
} // mega

