#include "gtest/gtest.h"
#include "mega/clock.h"
#include <thread>
#include <chrono>

using mega::ScopedSteadyClock;
using namespace std::chrono_literals;

TEST(Clock, ScopedSteadyClockMeasurePassedTimeCorrectly)
{
    ScopedSteadyClock clock;
    std::this_thread::sleep_for(1000ms);
    auto duration = clock.passedTime();
    // The expected duration would be close to 1000ms
    // We allow 500ms for the margin
    ASSERT_TRUE(duration > 500ms && duration < 1500ms);
}
