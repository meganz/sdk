#pragma once

#include <mega/common/date_time.h>

#ifdef HAS_DISTINCT_TIME_T

#include <ctime>

namespace mega
{
namespace common
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
} // common
} // mega

#endif // HAS_DISTINCT_TIME_T
