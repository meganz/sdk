#pragma once

#include <chrono>

namespace mega {

//
// T can be steady_clock, system_clock or high_resolution_clock
//
template<typename T>
class ScopedClock
{
public:
    using time_point = typename T::time_point;
    using duration   = typename T::duration;

    duration passedTime() const
    {
        return T::now() - mStart;
    }
private:
    time_point mStart{T::now()};
};

using ScopedSteadyClock = ScopedClock<std::chrono::steady_clock>;

}