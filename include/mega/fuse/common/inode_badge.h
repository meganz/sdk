#pragma once

#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/inode_badge_forward.h>
#include <mega/fuse/common/inode_forward.h>

namespace mega
{
namespace fuse
{

class InodeBadge
{
    friend class DirectoryInode;
    friend class FileInode;
    friend class Inode;

    InodeBadge() = default;

public:
    InodeBadge(const InodeBadge& other) = default;

    ~InodeBadge() = default;
}; // InodeBadge

} // fuse
} // mega

