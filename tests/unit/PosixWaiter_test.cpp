#include "mega/posix/megawaiter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <thread>
#include <unistd.h>

using ::mega::dstime;
using ::mega::ExecOnReady;
using ::mega::PosixWaiter;
using ::mega::Waiter;

TEST(PosixWaiter, NotifyWakesWait)
{
    PosixWaiter w;

    w.init(3000); // 300 seconds; the test must not depend on this elapsing
    w.notify();

    const auto start = std::chrono::steady_clock::now();
    const int r = w.wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(r & Waiter::NEEDEXEC);
    EXPECT_LT(elapsed, std::chrono::seconds(5)) << "wait() did not return promptly";
}

// notify() is callable from any thread; NotifyWakesWait alone only exercises the
// sequential case. Meaningful under -DENABLE_TSAN=ON.
TEST(PosixWaiter, NotifyFromAnotherThreadWakesWait)
{
    PosixWaiter w;

    w.init(3000);

    std::thread waker(
        [&w]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            w.notify();
        });

    const auto start = std::chrono::steady_clock::now();
    const int r = w.wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    waker.join();

    EXPECT_TRUE(r & Waiter::NEEDEXEC);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(PosixWaiter, TimeoutRequestsExec)
{
    PosixWaiter w;

    w.init(1); // one decisecond

    EXPECT_TRUE(w.wait() & Waiter::NEEDEXEC);
}

// The end-to-end half of FdWatchSet.StaleDescriptorIsReported: a stale
// descriptor must not leave the main loop spinning without exec().
TEST(PosixWaiter, StaleDescriptorRequestsExec)
{
    // Constructed before the pipe below, not after: pipe() hands back the lowest
    // free numbers, so in the other order PosixWaiter's own wakeup pipe lands on
    // `stale` and the two registrations merge into a live descriptor -- the test
    // then passes on the timeout path without ever polling a stale one.
    PosixWaiter w;

    int stale = -1;
    {
        int fds[2] = {-1, -1};
        ASSERT_EQ(0, ::pipe(fds));
        stale = fds[0];
        ::close(fds[0]);
        ::close(fds[1]);
    }

    w.init(1);
    w.watchReadable(stale, ExecOnReady::No);

    EXPECT_TRUE(w.wait() & Waiter::NEEDEXEC);
}

// The three clamping edges, plus the NEVER case.
TEST(PosixWaiter, TimeoutMsForClampsBothEnds)
{
    EXPECT_EQ(-1, PosixWaiter::timeoutMsFor(NEVER)) << "no ceiling means wait forever";

    constexpr dstime cap = std::numeric_limits<int>::max() / 100;

    // The largest dstime that still converts exactly: the division truncates, so
    // cap * 100 lands just under INT_MAX rather than on it.
    EXPECT_EQ(static_cast<int>(cap) * 100, PosixWaiter::timeoutMsFor(cap))
        << "the largest non-clamping value must convert exactly";

    EXPECT_EQ(std::numeric_limits<int>::max(), PosixWaiter::timeoutMsFor(cap + 1))
        << "overflow must clamp to the int ceiling, not degrade to -1 " << "(an infinite wait)";

    EXPECT_EQ(0, PosixWaiter::timeoutMsFor(-1))
        << "an expired deadline must poll immediately, not wait forever";

    EXPECT_EQ(100, PosixWaiter::timeoutMsFor(1)) << "one decisecond is 100ms";
}
