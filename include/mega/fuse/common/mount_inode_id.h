#pragma once

#include <cstdint>
#include <string>

#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/mount_inode_id_forward.h>

namespace mega
{
namespace fuse
{

class MountInodeID
{
    std::uint64_t mValue;

public:
    explicit MountInodeID(InodeID id);

    explicit MountInodeID(std::uint64_t value);

    MountInodeID(const MountInodeID& other) = default;

    MountInodeID& operator=(const MountInodeID& rhs) = default;

    bool operator==(const MountInodeID& rhs) const;

    bool operator<(const MountInodeID& rhs) const;

    bool operator!=(const MountInodeID& rhs) const;

    std::uint64_t get() const;
}; // MountInodeID

std::string toString(MountInodeID id);

} // fuse
} // mega

