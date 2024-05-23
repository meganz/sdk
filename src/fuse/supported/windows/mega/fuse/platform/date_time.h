#pragma once

#include <mega/fuse/platform/windows.h>

#include <mega/fuse/common/date_time.h>

namespace mega
{
namespace fuse
{
namespace detail
{

template<>
struct TimeValueTraits<UINT64>
{
    static const UINT64 offset = 11644473600ull;
    static const UINT64 scale = 10000000ull;

    static std::uint64_t from(const UINT64 value)
    {
        return (value / scale) - offset;
    }

    static UINT64 to(std::uint64_t value)
    {
        return (value + offset) * scale;
    }
}; // TimeValueTraits<UINT64>

template<>
struct TimeValueTraits<FILETIME>
{
    static std::uint64_t from(const FILETIME& value)
    {
        UINT64 low = static_cast<UINT64>(value.dwLowDateTime);
        UINT64 high = static_cast<UINT64>(value.dwHighDateTime);

        return TimeValueTraits<UINT64>::from((high << 32) | low);
    }

    static FILETIME to(std::uint64_t value)
    {
        auto temp = TimeValueTraits<UINT64>::to(value);
        FILETIME result;

        result.dwLowDateTime = static_cast<DWORD>(temp);
        result.dwHighDateTime = static_cast<DWORD>(temp >> 32);

        return result;
    }
}; // TimeValueTraits<FILETIME>

} // detail
} // fuse
} // fuse

