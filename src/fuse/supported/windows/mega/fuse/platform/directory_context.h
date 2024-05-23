#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/context.h>
#include <mega/fuse/platform/directory_context_forward.h>
#include <mega/fuse/platform/library.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class DirectoryContext
  : public Context
{
    // Translates a name to a child index.
    using NameIndexPair = std::pair<std::string, std::size_t>;

    // Tracks all known name-index mappings.
    using NameIndexVector = std::vector<NameIndexPair>;

    // Retrieve this directory's children if necessary.
    void populate() const;

    // Who are this directory's children?
    mutable InodeRefVector mChildren;

    // Maps child names to child indices.
    mutable NameIndexVector mChildrenByName;

    // What directory are we iterating over?
    DirectoryInodeRef mDirectory;

    // Serializes access to instance members.
    mutable std::mutex mLock;

    // Offset of first non-link child.
    mutable NameIndexVector::size_type mOffset;

    // Is this a root directory?
    bool mIsRoot;

    // How we retrieved this directory's children?
    mutable bool mPopulated;

public:
    DirectoryContext(DirectoryInodeRef directory,
                     fuse::Mount& mount,
                     bool isRoot);

    ~DirectoryContext();

    // Check if this context represents a directory.
    DirectoryContext* directory() override;

    // Retrieve a reference to the specified child.
    InodeRef get(const std::string& name) const;

    // Retrieve a listing of this directory's children.
    void get(const std::string& marker,
             PVOID buffer,
             ULONG length,
             const Mount& mount,
             ULONG& numWritten) const;

    // What inode does this context represent?
    InodeRef inode() const override;
}; // DirectoryContext

} // platform
} // fuse
} // mega

