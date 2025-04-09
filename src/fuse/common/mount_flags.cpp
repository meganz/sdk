#include <cassert>
#include <stdexcept>

#include <mega/common/query.h>
#include <mega/common/scoped_query.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_flags.h>

namespace mega
{
namespace fuse
{

using namespace common;

bool MountFlags::operator==(const MountFlags& rhs) const
{
    return mName == rhs.mName
           && mEnableAtStartup == rhs.mEnableAtStartup
           && mPersistent == rhs.mPersistent
           && mReadOnly == rhs.mReadOnly;
}

bool MountFlags::operator!=(const MountFlags& rhs) const
{
    return !(*this == rhs);
}

MountFlags MountFlags::deserialize(Query& query)
try
{
    MountFlags flags;

    flags.mEnableAtStartup = query.field("enable_at_startup").get<bool>();
    flags.mName = query.field("name").get<std::string>();
    flags.mPersistent = query.field("persistent").get<bool>();
    flags.mReadOnly = query.field("read_only").get<bool>();

    // Sanity.
    assert(!flags.mEnableAtStartup || flags.mPersistent);

    return flags;
}
catch (std::runtime_error& exception)
{
   FUSEErrorF("Unable to deserialize mount flags: %s", exception.what());

   throw;
}

MountFlags MountFlags::deserialize(ScopedQuery& query)
{
    return deserialize(query.query());
}

void MountFlags::serialize(Query& query) const
try
{
    // Sanity.
    assert(!mEnableAtStartup || mPersistent);

    query.param(":enable_at_startup").set(mEnableAtStartup);
    query.param(":name").set(mName);
    query.param(":persistent").set(mPersistent);
    query.param(":read_only").set(mReadOnly);
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to serialize mount flags: %s", exception.what());

    throw;
}

void MountFlags::serialize(ScopedQuery& query) const
{
    serialize(query.query());
}

} // fuse
} // mega

