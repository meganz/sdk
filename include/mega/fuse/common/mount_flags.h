#pragma once

#include <string>

#include <mega/fuse/common/mount_flags.h>
#include <mega/fuse/common/query_forward.h>
#include <mega/fuse/common/scoped_query_forward.h>

namespace mega
{
namespace fuse
{

struct MountFlags
{
    bool operator==(const MountFlags& rhs) const;

    bool operator!=(const MountFlags& rhs) const;

    static MountFlags deserialize(Query& query);
    static MountFlags deserialize(ScopedQuery& query);

    void serialize(Query& query) const;
    void serialize(ScopedQuery& query) const;

    std::string mName;
    bool mEnableAtStartup = false;
    bool mPersistent = false;
    bool mReadOnly = false;
}; // MountFlags

} // fuse
} // mega

