#include <mega/common/node_info.h>
#include <mega/fuse/common/date_time.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/printers.h>

namespace mega
{
namespace fuse
{

using namespace common;

static std::ostream& operator<<(std::ostream& ostream, accesslevel_t permissions);

static const std::string indent = std::string(6, ' ');

std::ostream& operator<<(std::ostream& ostream, const DateTime& value)
{
    return ostream << toString(value);
}

template<typename T>
auto operator<<(std::ostream& ostream, const T& value)
  -> typename testing::EnableIfInfoLike<T, std::ostream>::type&
{
    using testing::id;
    using testing::parentID;
    using testing::toString;

    return ostream << "\n"
                   << indent
                   << "mID: "
                   << toString(id(value))
                   << "\n"
                   << indent
                   << "mIsDirectory: "
                   << (value.mIsDirectory ? "TRUE" : "FALSE")
                   << "\n"
                   << indent
                   << "mModified: "
                   << DateTime(value.mModified)
                   << "\n"
                   << indent
                   << "mName: "
                   << value.mName
                   << "\n"
                   << indent
                   << "mParentID: "
                   << toString(parentID(value))
                   << "\n"
                   << indent
                   << "mPermissions: "
                   << value.mPermissions
                   << "\n"
                   << indent
                   << "mSize: "
                   << value.mSize;
}

void PrintTo(const MountEventType type, std::ostream* ostream)
{
    *ostream << toString(type);
}

void PrintTo(const MountResult result, std::ostream* ostream)
{
    *ostream << toString(result);
}

std::ostream& operator<<(std::ostream& ostream, accesslevel_t permissions)
{
    switch (permissions)
    {
    case RDONLY:
        return ostream << "RDONLY";
    case RDWR:
        return ostream << "RDWR";
    case FULL:
        return ostream << "FULL";
    case OWNER:
        return ostream << "OWNER";
    case OWNERPRELOGIN:
        return ostream << "OWNER-PRELOGIN";
    default:
        break;
    }

    return ostream << "UNKNOWN";
}

template std::ostream& operator<<(std::ostream&, const InodeInfo&);
template std::ostream& operator<<(std::ostream&, const NodeInfo&);

} // fuse
} // mega

