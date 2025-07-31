#pragma once

#include <string>

#include <mega/common/query_forward.h>
#include <mega/common/scoped_query_forward.h>
#include <mega/fuse/common/mount_flags.h>

namespace mega
{
namespace fuse
{

struct MountFlags
{
    bool operator==(const MountFlags& rhs) const;

    bool operator!=(const MountFlags& rhs) const;

    static MountFlags deserialize(common::Query& query);
    static MountFlags deserialize(common::ScopedQuery& query);

    void serialize(common::Query& query) const;
    void serialize(common::ScopedQuery& query) const;

    std::string mName;
    bool mEnableAtStartup = false;
    bool mPersistent = false;
    bool mReadOnly = false;
    bool mAllowSelfAccess = false; // Only used by test to allow self process to access the mount.
}; // MountFlags

} // fuse
} // mega

