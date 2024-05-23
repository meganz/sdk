#pragma once

#include <cstdint>
#include <sstream>
#include <string>

#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/mount_inode_id_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

class InodeID
{
    std::uint64_t mValue;

public:
    InodeID();

    explicit InodeID(MountInodeID id);

    explicit InodeID(NodeHandle handle);

    explicit InodeID(std::uint64_t value);

    InodeID(const InodeID& other) = default;

    explicit operator NodeHandle() const;

    operator bool() const;

    InodeID& operator=(const InodeID& rhs) = default;

    bool operator==(const InodeID& rhs) const;

    bool operator==(const NodeHandle& rhs) const;

    bool operator<(const InodeID& rhs) const;

    bool operator!=(const InodeID& rhs) const;

    bool operator!=(const NodeHandle& rhs) const;

    bool operator!() const;

    static InodeID fromFileName(const std::string& name);

    std::uint64_t get() const;

    bool synthetic() const;
}; // InodeID

std::string toFileName(InodeID id);

std::string toString(InodeID id);

} // fuse
} // mega

