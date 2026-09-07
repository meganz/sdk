#pragma once

#include <mega/common/query_forward.h>
#include <mega/file_service/file_id.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/mount_inode_id_forward.h>
#include <mega/types.h>

#include <cstdint>
#include <string>

namespace mega
{
namespace common
{

template<>
struct SerializationTraits<fuse::InodeID>: SerializationTraits<file_service::FileID>
{}; // SerializationTraits<fuse::InodeID>

} // common

namespace fuse
{

class InodeID: public file_service::FileID
{
    explicit InodeID(std::uint64_t id);

public:
    using FileID::FileID;

    InodeID(file_service::FileID id);

    using FileID::from;

    static InodeID from(MountInodeID id);
}; // InodeID

} // fuse
} // mega

