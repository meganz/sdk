#include <stdexcept>

#include <mega/common/query.h>
#include <mega/common/scoped_query.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_info.h>

namespace mega
{
namespace fuse
{

using namespace common;

bool MountInfoNameLess::operator()(const MountInfo& lhs, const MountInfo& rhs) const
{
    return lhs.name() < rhs.name();
}

bool MountInfoPathLess::operator()(const MountInfo& lhs, const MountInfo& rhs) const
{
    return lhs.mPath < rhs.mPath;
}

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
    info.mHandle = query.field("id").get<NodeHandle>();
    info.mPath = NormalizedPath();

    if (!query.field("path").null())
        info.mPath = query.field("path").get<LocalPath>();

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

    query.param(":id").set(mHandle);
    query.param(":path").set(nullptr);

    if (!mPath.empty())
        query.param(":path").set<LocalPath>(mPath);
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

