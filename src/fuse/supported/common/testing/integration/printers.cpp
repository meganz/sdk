#include <mega/common/date_time.h>
#include <mega/common/node_info.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/printers.h>
#include <mega/fuse/common/testing/utility.h>

#include <ostream>

namespace mega
{
namespace fuse
{

using namespace common;

static std::ostream& operator<<(std::ostream& ostream, accesslevel_t permissions);

static const std::string indent = std::string(6, ' ');

std::ostream& operator<<(std::ostream& ostream, const InodeInfo& value)
{
    return ostream << "\n"
                   << indent << "mID: " << toString(value.mID) << "\n"
                   << indent << "mIsDirectory: " << (value.mIsDirectory ? "TRUE" : "FALSE") << "\n"
                   << indent << "mModified: " << DateTime(value.mModified) << "\n"
                   << indent << "mName: " << value.mName << "\n"
                   << indent << "mParentID: " << toString(value.mParentID) << "\n"
                   << indent << "mPermissions: " << value.mPermissions << "\n"
                   << indent << "mSize: " << value.mSize;
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

} // fuse
} // mega
