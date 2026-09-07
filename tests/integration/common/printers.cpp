#include <mega/common/date_time.h>
#include <mega/common/node_info.h>
#include <mega/utils.h>

#include <ostream>

namespace mega
{
namespace common
{

std::ostream& operator<<(std::ostream& ostream, const DateTime& value)
{
    return ostream << toString(value);
}

std::ostream& operator<<(std::ostream& ostream, const NodeInfo& value)
{
    static const std::string indent(6, ' ');

    return ostream << "\n"
                   << indent << "mID: " << toNodeHandle(value.mHandle) << "\n"
                   << indent << "mIsDirectory: " << (value.mIsDirectory ? "TRUE" : "FALSE") << "\n"
                   << indent << "mModified: " << DateTime(value.mModified) << "\n"
                   << indent << "mName: " << value.mName << "\n"
                   << indent << "mParentID: " << toNodeHandle(value.mParentHandle) << "\n"
                   << indent << "mPermissions: " << value.mPermissions << "\n"
                   << indent << "mSize: " << value.mSize;
}

} // common
} // mega
