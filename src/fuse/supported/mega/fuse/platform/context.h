#pragma once

#include <mega/fuse/common/badge.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_forward.h>
#include <mega/fuse/platform/context_forward.h>
#include <mega/fuse/platform/directory_context_forward.h>
#include <mega/fuse/platform/file_context_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// Represents the context of an arbitrary filesystem entity.
class Context
{
    fuse::Mount* mMount;

protected:
    Context(fuse::Mount& mount);

public:
    virtual ~Context();

    // Check if this context represents a directory.
    virtual DirectoryContext* directory();

    // Check if this context represents a file.
    virtual FileContext* file();

    // Retrieve a description of the entity this context represents.
    InodeInfo info() const;

    // What inode does this context represent?
    virtual InodeRef inode() const = 0;

    // What mount created this context?
    fuse::Mount& mount() const;
}; // Context

} // platform
} // fuse
} // mega

