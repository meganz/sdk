// none relevant small groups tests not worth of putting to its own test files
// are put here

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mega/gfx/isolatedprocess.h"
#include "mega/scoped_timer.h"

#include <chrono>
#include <vector>

using mega::LocalPath;
using mega::ScopedSteadyTimer;
using std::chrono::seconds;
using Params = mega::GfxIsolatedProcess::Params;

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

TEST(Isolatedprocess, ParamsConstructedWithDefaultAsExpected)
{
    const std::string exec{"the/path is/exe"};
    const std::string expectedExec{LocalPath::fromAbsolutePath(exec).toPath(false)};

    Params params{"endpoint", exec};
    ASSERT_THAT(params.toArgs(), testing::ElementsAre(expectedExec, "-n=endpoint", "-l=60"));
}

TEST(Isolatedprocess, ParamsConstructedWithExtraParametersAsExpected)
{
    const std::string exec{"the/path is/exe"};
    const std::string expectedExec{LocalPath::fromAbsolutePath(exec).toPath(false)};
    const auto rawArgs = std::vector<std::string>{"-t=10", "-d=the/path is/log"};

    Params params{"endpoint", exec, seconds{20}, rawArgs};
    ASSERT_THAT(
        params.toArgs(),
        testing::ElementsAre(expectedExec, "-n=endpoint", "-l=20", "-t=10", "-d=the/path is/log"));
}

// Duplicate parameters are also retained.
TEST(Isolatedprocess, ParamsConstructedWithDuplidatedExtraParametersAreKeptAsExpected)
{
    const std::string exec{"the/path is/exe"};
    const std::string expectedExec{LocalPath::fromAbsolutePath(exec).toPath(false)};
    const auto rawArgs = std::vector<std::string>{"-n=anotherEndplint", "-l=20"};

    Params params{"endpoint", exec, seconds{20}, rawArgs};
    ASSERT_THAT(
        params.toArgs(),
        testing::ElementsAre(expectedExec, "-n=endpoint", "-l=20", "-n=anotherEndplint", "-l=20"));
}
