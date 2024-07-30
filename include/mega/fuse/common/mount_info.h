#pragma once

#include <mega/fuse/common/mount_flags.h>
#include <mega/fuse/common/mount_info_forward.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/common/scoped_query_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

struct MountInfo
{
    bool operator==(const MountInfo& rhs) const;

    bool operator!=(const MountInfo& info) const;

    static MountInfo deserialize(ScopedQuery& query);

    void serialize(ScopedQuery& query) const;

    MountFlags mFlags;
    NodeHandle mHandle;
    NormalizedPath mPath;
}; // MountInfo

} // fuse
} // mega

