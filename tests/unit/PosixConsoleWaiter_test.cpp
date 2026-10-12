#include "mega/posix/megaconsolewaiter.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <stdexcept>
#include <string>
#include <unistd.h>

using ::mega::PosixConsoleWaiter;
using ::mega::Waiter;

namespace
{

// PosixConsoleWaiter::wait() hardcodes STDIN_FILENO, so stdin is substituted
// with a pipe read end and restored afterwards. It never constructs
// PosixConsole, so the substitute not being a tty does not matter.
class StdinSubstitute
{
public:
    // Throws rather than EXPECT_: a half-built substitute leaves the test
    // asserting against the process's real stdin, a false pass or failure with
    // nothing to point at. The destructor does not run when a constructor
    // throws, so each exit closes what it opened; stdin was never replaced.
    StdinSubstitute()
    {
        if (::pipe(mFds) != 0)
        {
            throw std::runtime_error("pipe() failed: " + std::to_string(errno));
        }

        mSaved = ::dup(STDIN_FILENO);
        if (mSaved < 0)
        {
            const int e = errno;
            ::close(mFds[0]);
            ::close(mFds[1]);
            throw std::runtime_error("cannot substitute stdin without a way to restore it: " +
                                     std::to_string(e));
        }

        if (::dup2(mFds[0], STDIN_FILENO) == -1)
        {
            const int e = errno;
            ::close(mSaved);
            ::close(mFds[0]);
            ::close(mFds[1]);
            throw std::runtime_error("dup2() onto stdin failed: " + std::to_string(e));
        }
    }

    ~StdinSubstitute()
    {
        if (mSaved >= 0)
        {
            ::dup2(mSaved, STDIN_FILENO);
            ::close(mSaved);
        }
        ::close(mFds[0]);
        ::close(mFds[1]);
    }

    StdinSubstitute(const StdinSubstitute&) = delete;
    StdinSubstitute& operator=(const StdinSubstitute&) = delete;

    void writeByte() const
    {
        EXPECT_EQ(1, ::write(mFds[1], "x", 1));
    }

private:
    int mFds[2] = {-1, -1};
    int mSaved = -1;
};

} // namespace

// The regression: on the old poll() path HAVESTDIN was returned unconditionally,
// because readiness could not be distinguished from registration.
TEST(PosixConsoleWaiter, HaveStdinOnlyWhenStdinIsReadable)
{
    {
        StdinSubstitute quiet;
        PosixConsoleWaiter w;

        w.init(1); // one decisecond, so wait() returns on timeout

        EXPECT_FALSE(w.wait() & Waiter::HAVESTDIN)
            << "nothing was written to stdin, so HAVESTDIN must not be set";
    }

    {
        StdinSubstitute ready;
        ready.writeByte();

        PosixConsoleWaiter w;

        w.init(1);

        EXPECT_TRUE(w.wait() & Waiter::HAVESTDIN)
            << "stdin has a byte pending, so HAVESTDIN must be set";
    }
}
