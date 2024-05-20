#pragma once

#include <string>

#include <mega/fuse/common/path_adapter.h>
#include <mega/fuse/platform/path_adapter_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{
namespace detail
{

struct PathAdapterTraits
{
    using SizeType   = std::string::size_type;
    using StringType = std::string;
    using ValueType  = std::string::value_type;

    static ValueType separator()
    {
        return '/';
    }

    static std::string toUTF8(const ValueType* data, SizeType length)
    {
        return std::string(data, length);
    }
}; // PathAdapterTraits

} // detail
} // platform
} // fuse
} // mega

