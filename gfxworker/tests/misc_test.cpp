#include "gtest/gtest.h"
#include "mega/gfx/isolatedprocess.h"
// none relevant small groups tests not worth of putting to its own test files
// are put here

TEST(Misc, CancelableSleeperCanBecancelledInNoTime)
{
    mega::CancellableSleeper sleeper;

    std::thread t ([&sleeper](){
        sleeper.sleep(std::chrono::milliseconds(24*60*60)); //absolute long
    });

    sleeper.cancel();

    if (t.joinable()) t.join();

}

TEST(Misc, GfxWorkerHelloBeaterCanGracefullyShutdownInNoTime)
{
    mega::GfxWorkerHelloBeater beater(std::chrono::seconds(24*60*60)); //absolute long
}
