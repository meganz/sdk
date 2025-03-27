#pragma once

#include <cstdint>
#include <string>

#include <mega/common/bind_handle.h>
#include <mega/common/node_info_forward.h>

#include <mega/types.h>

namespace mega
{
namespace common
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

} // common
} // mega

