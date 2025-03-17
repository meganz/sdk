#include <stdexcept>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/query.h>
#include <mega/fuse/common/scoped_query.h>

namespace mega
{
namespace fuse
{

bool MountInfo::operator==(const MountInfo& rhs) const
{
    return mPath == rhs.mPath
           && mFlags == rhs.mFlags
           && mHandle == rhs.mHandle;
}

bool MountInfo::operator!=(const MountInfo& rhs) const
{
    return !(*this == rhs);
}

void MountInfo::name(const std::string& name)
{
    mFlags.mName = name;
}

const std::string& MountInfo::name() const
{
    return mFlags.mName;
}

MountInfo MountInfo::deserialize(Query& query)
try
{
    MountInfo info;

    info.mFlags = MountFlags::deserialize(query);
    info.mHandle = query.field("id");
    info.mPath = query.field("path");

    return info;
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to deserialize mount info: %s", exception.what());

    throw;
}

MountInfo MountInfo::deserialize(ScopedQuery& query)
{
    return deserialize(query.query());
}

void MountInfo::serialize(Query& query) const
try
{
    mFlags.serialize(query);

    query.param(":id") = mHandle;
    query.param(":path") = mPath;
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to serialize mount info: %s", exception.what());

    throw;
}

void MountInfo::serialize(ScopedQuery& query) const
{
    serialize(query.query());
}

} // fuse
} // mega

