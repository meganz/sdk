#include "gtest/gtest.h"
#include "mega/scoped_timer.h"
#include <thread>
#include <chrono>

using mega::ScopedSteadyTimer;
using namespace std::chrono_literals;

TEST(ScopedTimer, ScopedSteadyTimerMeasurePassedTimeCorrectly)
{
    ScopedSteadyTimer timer;
    // sleep_for blocks the execution of the current thread for at least the specified
    // sleep_duration. It may block for longer than sleep_duration due to scheduling or resource
    // contention delays.
    std::this_thread::sleep_for(1000ms);
    auto duration = timer.passedTime();
    ASSERT_GE(duration, 1000ms); // Never could be less that 1s
}
