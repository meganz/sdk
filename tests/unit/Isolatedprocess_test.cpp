// none relevant small groups tests not worth of putting to its own test files
// are put here

#include "gtest/gtest.h"
#include "mega/gfx/isolatedprocess.h"
#include "mega/scoped_timer.h"

#include <chrono>

using mega::ScopedSteadyTimer;
using std::chrono::seconds;

TEST(Isolatedprocess, CancelableSleeperCanBecancelledInNoTime)
{
    ScopedSteadyTimer timer;

    mega::CancellableSleeper sleeper;
    std::thread t ([&sleeper](){
        sleeper.sleep(seconds(60)); // long enough
    });

    sleeper.cancel();

    if (t.joinable()) t.join();

    // cancel should be done immediately, but we don't want a
    // too short number that the result could be affected by disturbance.
    ASSERT_TRUE(timer.passedTime() < seconds(10));
}

//
// Test hellobeater can be shutdown quickly
//
TEST(Isolatedprocess, GfxWorkerHelloBeaterCanGracefullyShutdownInNoTime)
{
    ScopedSteadyTimer timer;
    {
        mega::HelloBeater beater(seconds(60), "__"); //long enough
    }
    // cancel should be done immediately, but we don't want a
    // too short number that the result could be affected by disturbance.
    ASSERT_TRUE(timer.passedTime() < seconds(10));
}
