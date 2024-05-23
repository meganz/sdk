#include <cassert>
#include <stdexcept>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_flags.h>
#include <mega/fuse/common/query.h>
#include <mega/fuse/common/scoped_query.h>

namespace mega
{
namespace fuse
{

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

MountFlags MountFlags::deserialize(ScopedQuery& query)
try
{
    MountFlags flags;

    flags.mEnableAtStartup = query.field("enable_at_startup");
    flags.mName = query.field("name").string();
    flags.mPersistent = query.field("persistent");
    flags.mReadOnly = query.field("read_only");

    // Sanity.
    assert(!flags.mEnableAtStartup || flags.mPersistent);

    return flags;
}
catch (std::runtime_error& exception)
{
   FUSEErrorF("Unable to deserialize mount flags: %s", exception.what());

   throw;
}

void MountFlags::serialize(ScopedQuery& query) const
try
{
    // Sanity.
    assert(!mEnableAtStartup || mPersistent);

    query.param(":enable_at_startup") = mEnableAtStartup;
    query.param(":name") = mName;
    query.param(":persistent") = mPersistent;
    query.param(":read_only") = mReadOnly;
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to serialize mount flags: %s", exception.what());

    throw;
}

} // fuse
} // mega

