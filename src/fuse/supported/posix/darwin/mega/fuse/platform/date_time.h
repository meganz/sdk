#pragma once

#include <ctime>

#include <mega/fuse/common/date_time.h>

namespace mega
{
namespace fuse
{
namespace detail
{

template<>
struct TimeValueTraits<time_t>
{
    static std::uint64_t from(time_t value)
    {
        return static_cast<std::uint64_t>(value);
    }

    static time_t to(std::uint64_t value)
    {
        return static_cast<time_t>(value);
    }
}; // TimeValueTraits<time_t>

} // detail
} // fuse
} // mega

