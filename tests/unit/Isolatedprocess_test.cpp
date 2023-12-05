// none relevant small groups tests not worth of putting to its own test files
// are put here

#include "gtest/gtest.h"
#include "mega/gfx/isolatedprocess.h"

#include <chrono>

using std::chrono::time_point;
using std::chrono::seconds;
using std::chrono::system_clock;
namespace
{
class DurationCounter
{
public:
    seconds duration() const;
private:
    time_point<system_clock> mStart{system_clock::now()};
};

seconds DurationCounter::duration() const
{
    return std::chrono::duration_cast<seconds>(system_clock::now() - mStart);
}

}

TEST(Isolatedprocess, CancelableSleeperCanBecancelledInNoTime)
{
    DurationCounter counter;

    mega::CancellableSleeper sleeper;
    std::thread t ([&sleeper](){
        sleeper.sleep(seconds(60)); // long enough
    });

    sleeper.cancel();

    if (t.joinable()) t.join();

    // cancel should be done immediately, but we don't want a
    // too short number that the result could be affected by disturbance.
    ASSERT_TRUE(counter.duration() < seconds(10));
}

//
// Test hellobeater can be shutdown quickly
//
TEST(Isolatedprocess, GfxWorkerHelloBeaterCanGracefullyShutdownInNoTime)
{
    DurationCounter counter;
    {
        mega::GfxWorkerHelloBeater beater(seconds(60), "__"); //long enough
    }
    // cancel should be done immediately, but we don't want a
    // too short number that the result could be affected by disturbance.
    ASSERT_TRUE(counter.duration() < seconds(10));
}
