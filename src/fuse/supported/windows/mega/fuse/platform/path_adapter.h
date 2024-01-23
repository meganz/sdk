#pragma once

#include <string>

#include <mega/fuse/common/path_adapter.h>
#include <mega/fuse/platform/path_adapter_forward.h>
#include <mega/fuse/platform/utility.h>

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
    using SizeType   = std::wstring::size_type;
    using StringType = std::wstring;
    using ValueType  = std::wstring::value_type;

    static ValueType separator()
    {
        return L'\\';
    }

    static std::string toUTF8(const ValueType* data, SizeType length)
    {
        return fromWideString(data, length);
    }
}; // PathAdapterTraits

} // detail
} // platform
} // fuse
} // mega

