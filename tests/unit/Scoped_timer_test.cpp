#include "gtest/gtest.h"
#include "mega/scoped_timer.h"
#include <thread>
#include <chrono>

using mega::ScopedSteadyTimer;
using namespace std::chrono_literals;

TEST(ScopedTimer, ScopedSteadyTimerMeasurePassedTimeCorrectly)
{
    ScopedSteadyTimer timer;
    std::this_thread::sleep_for(1000ms);
    auto duration = timer.passedTime();
    // The expected duration would be close to 1000ms
    // We allow 500ms for the margin
    ASSERT_TRUE(duration > 500ms && duration < 1500ms);
}
