#pragma once

#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/platform/directory_context_forward.h>
#include <mega/fuse/platform/mount_forward.h>
#include <mega/fuse/platform/read_directory_functions_forward.h>
#include <mega/fuse/platform/request_forward.h>

#include <cstddef>
#include <string>

struct stat;

namespace mega
{
namespace fuse
{
namespace platform
{

struct ReadDirectoryFunctions
{
    // Called to add a directory entry to buffer.
    virtual std::size_t addEntry(const struct stat& attributes,
                                 char* buffer,
                                 const std::string& name,
                                 off_t offset,
                                 Request request,
                                 std::size_t size) = 0;

    // Called to pin an inode in memory.
    virtual void pin(const InodeInfo& info, InodeRef& inode, Mount& mount) = 0;
}; // DoReadDirectoryFunctions

} // platform
} // fuse
} // mega
