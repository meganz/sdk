#pragma once

#include <cstdint>
#include <string>

#include <mega/fuse/common/bind_handle.h>
#include <mega/fuse/common/node_info_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

struct NodeInfo
{
    BindHandle mBindHandle;
    NodeHandle mHandle;
    bool mIsDirectory;
    m_time_t mModified;
    std::string mName;
    NodeHandle mParentHandle;
    accesslevel_t mPermissions;
    m_off_t mSize;
}; // NodeInfo

} // fuse
} // mega

