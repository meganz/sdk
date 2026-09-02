#include "mega/posix/megafdwatchset.h"

#include <gtest/gtest.h>
#include <sys/resource.h>
#include <sys/select.h> // FD_SETSIZE
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using ::mega::ExecOnReady;
using ::mega::FdWatchSet;

namespace
{

// Owns a pipe for the duration of a test. Pipes and AF_UNIX socket pairs only:
// kernel objects with no filesystem or network dependency, so test_unit stays
// free of I/O.
class Pipe
{
public:
    // Throws rather than EXPECT_: handing out -1 would trip assert(fd >= 0) and
    // abort the whole binary, where gtest turns a throw into one test failing.
    Pipe()
    {
        if (::pipe(mFds) != 0)
        {
            throw std::runtime_error("pipe() failed: " + std::to_string(errno));
        }
    }

    ~Pipe()
    {
        closeRead();
        closeWrite();
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    int readEnd() const
    {
        return mFds[0];
    }

    int writeEnd() const
    {
        return mFds[1];
    }

    void writeByte() const
    {
        const ssize_t w = ::write(mFds[1], "x", 1);
        EXPECT_EQ(1, w);
    }

    void closeRead()
    {
        if (mFds[0] >= 0)
        {
            ::close(mFds[0]);
            mFds[0] = -1;
        }
    }

    void closeWrite()
    {
        if (mFds[1] >= 0)
        {
            ::close(mFds[1]);
            mFds[1] = -1;
        }
    }

private:
    int mFds[2] = {-1, -1};
};

// Owns a connected socket pair: one descriptor readable and writable at once,
// which is what tells a merged registration from an overwritten one.
class SocketPair
{
public:
    SocketPair()
    {
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, mFds) != 0)
        {
            throw std::runtime_error("socketpair() failed: " + std::to_string(errno));
        }
    }

    ~SocketPair()
    {
        for (int& fd: mFds)
        {
            if (fd >= 0)
            {
                ::close(fd);
                fd = -1;
            }
        }
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    int end() const
    {
        return mFds[0];
    }

    void writeByteFromPeer() const
    {
        const ssize_t w = ::write(mFds[1], "x", 1);
        EXPECT_EQ(1, w);
    }

private:
    int mFds[2] = {-1, -1};
};

// Restores the RLIMIT_NOFILE soft limit on every exit path, so a raised limit
// does not leak into later tests sharing this process.
struct RlimitRestorer
{
    explicit RlimitRestorer(const rlimit& orig):
        mOriginal(orig)
    {}

    ~RlimitRestorer()
    {
        setrlimit(RLIMIT_NOFILE, &mOriginal);
    }

    rlimit mOriginal;
};

// Raises the RLIMIT_NOFILE soft limit to `needed` if it is lower. Returns an
// empty string on success, else why not -- for the caller to GTEST_SKIP() on,
// since GTEST_SKIP() only skips from the test body.
std::string ensureNofileAtLeast(rlim_t needed, std::unique_ptr<RlimitRestorer>& restorer)
{
    rlimit lim{};
    if (getrlimit(RLIMIT_NOFILE, &lim) != 0)
    {
        return "getrlimit(RLIMIT_NOFILE) failed: " + std::to_string(errno);
    }

    if (lim.rlim_cur >= needed)
    {
        return {};
    }

    if (lim.rlim_max < needed)
    {
        return "RLIMIT_NOFILE hard limit is " + std::to_string(lim.rlim_max) + ", need " +
               std::to_string(needed);
    }

    rlimit raised = lim;
    raised.rlim_cur = needed;
    if (setrlimit(RLIMIT_NOFILE, &raised) != 0)
    {
        return "could not raise the RLIMIT_NOFILE soft limit from " + std::to_string(lim.rlim_cur) +
               " to " + std::to_string(needed);
    }

    restorer = std::make_unique<RlimitRestorer>(lim);
    return {};
}

} // namespace

TEST(FdWatchSet, ConstructsAndWaitsWithNothingRegistered)
{
    FdWatchSet set;

    set.clear();

    EXPECT_EQ(0, set.wait(0));
}

// The bug this ticket is about: the old poll() path answered "was registered"
// rather than "is ready", a std::set<int> having nowhere to put revents.
TEST(FdWatchSet, ReadinessIsNotRegistration)
{
    Pipe p;
    FdWatchSet set;

    set.clear();
    set.watchReadable(p.readEnd(), ExecOnReady::Yes);

    EXPECT_EQ(0, set.wait(50)) << "nothing was written, so this must time out";
    EXPECT_FALSE(set.isReadable(p.readEnd()));
}

// Both directions on one descriptor must merge into a single entry: a watch()
// that overwrote events would drop whichever direction was registered first.
TEST(FdWatchSet, BothDirectionsOnOneFd)
{
    SocketPair s;
    FdWatchSet set;

    set.clear();
    set.watchReadable(s.end(), ExecOnReady::Yes);
    set.watchWritable(s.end(), ExecOnReady::Yes);
    s.writeByteFromPeer();

    ASSERT_GT(set.wait(1000), 0);
    EXPECT_TRUE(set.isReadable(s.end())) << "a byte is pending from the peer";
    EXPECT_TRUE(set.isWritable(s.end())) << "the send buffer is empty";
}

// Contract sentence 4. Without isReady()'s events filter, poll()'s POLLHUP on a
// read-only registration would read as writable, which select() could never do.
TEST(FdWatchSet, ReadOnlyRegistrationNeverReportsWritable)
{
    Pipe p;
    FdWatchSet set;

    set.clear();
    set.watchReadable(p.readEnd(), ExecOnReady::Yes);
    p.closeWrite(); // read end now reports POLLHUP

    ASSERT_GT(set.wait(1000), 0);
    EXPECT_TRUE(set.isReadable(p.readEnd())) << "EOF counts as readable, as under select()";
    EXPECT_FALSE(set.isWritable(p.readEnd())) << "never registered for writing";
}

// Unlike ReadinessIsNotRegistration, this pins that wait() really blocked for
// the requested duration rather than returning 0 for some other reason.
TEST(FdWatchSet, TimeoutReturnsZero)
{
    Pipe p;
    FdWatchSet set;

    set.clear();
    set.watchReadable(p.readEnd(), ExecOnReady::Yes);

    constexpr int TimeoutMs = 200;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(0, set.wait(TimeoutMs));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GE(elapsed, std::chrono::milliseconds(TimeoutMs / 2))
        << "wait() returned 0 too quickly to have actually waited out the timeout";
    EXPECT_FALSE(set.isReadable(p.readEnd()));
}

TEST(FdWatchSet, ClearReleasesRegistrations)
{
    Pipe p;
    FdWatchSet set;

    set.clear();
    set.watchReadable(p.readEnd(), ExecOnReady::Yes);
    p.writeByte();
    ASSERT_GT(set.wait(1000), 0);
    ASSERT_TRUE(set.isReadable(p.readEnd()));

    set.clear();

    EXPECT_FALSE(set.isReadable(p.readEnd())) << "clear() must drop the registration";
    EXPECT_EQ(0, set.wait(0)) << "nothing is registered, so nothing can be ready";
}

// Readiness pending before a clear() must still be seen after re-registering.
// Catches a clear() that drops registrations without resetting mIndexByFd.
TEST(FdWatchSet, PendingDataStillReadableAfterRebuild)
{
    Pipe ready;
    Pipe quiet;
    FdWatchSet set;

    set.clear();
    set.watchReadable(ready.readEnd(), ExecOnReady::Yes);
    set.watchReadable(quiet.readEnd(), ExecOnReady::Yes);
    ready.writeByte();

    ASSERT_GT(set.wait(1000), 0);
    EXPECT_TRUE(set.isReadable(ready.readEnd()));
    EXPECT_FALSE(set.isReadable(quiet.readEnd()));

    // The byte is still unread, so after a clear() and a fresh registration the
    // same descriptor must come back readable and the quiet one must not.
    set.clear();
    set.watchReadable(ready.readEnd(), ExecOnReady::Yes);
    set.watchReadable(quiet.readEnd(), ExecOnReady::Yes);

    ASSERT_GT(set.wait(1000), 0);
    EXPECT_TRUE(set.isReadable(ready.readEnd()));
    EXPECT_FALSE(set.isReadable(quiet.readEnd()));
}

// What actually breaks the two-way invariant: a scattered readable subset plus
// capacity reuse after clear(). Asserts the whole readable set, not one entry.
TEST(FdWatchSet, PartialReadinessAcrossManyDescriptors)
{
    constexpr size_t Count = 40;
    std::vector<std::unique_ptr<Pipe>> pipes;
    for (size_t i = 0; i < Count; ++i)
    {
        pipes.push_back(std::make_unique<Pipe>());
    }

    FdWatchSet set;

    auto runRound = [&](const std::vector<size_t>& toMakeReady)
    {
        set.clear();
        for (const auto& p: pipes)
        {
            set.watchReadable(p->readEnd(), ExecOnReady::Yes);
        }
        for (const size_t i: toMakeReady)
        {
            pipes[i]->writeByte();
        }

        ASSERT_GT(set.wait(1000), 0);

        std::set<int> expected;
        for (const size_t i: toMakeReady)
        {
            expected.insert(pipes[i]->readEnd());
        }

        std::set<int> actual;
        for (const auto& p: pipes)
        {
            if (set.isReadable(p->readEnd()))
            {
                actual.insert(p->readEnd());
            }
        }

        EXPECT_EQ(expected, actual);
    };

    // Scattered indices, deliberately not the first or last few.
    const std::vector<size_t> firstRound = {3, 4, 17, 28, 39};
    ASSERT_NO_FATAL_FAILURE(runRound(firstRound));

    // A second round over a different subset, reusing the table's capacity: a
    // clear() that forgets to reset slots shows up here. poll() before each
    // read because they block, and test_unit has no per-test timeout, so a
    // missing byte would hang the binary instead of failing the test.
    for (const size_t i: firstRound)
    {
        struct pollfd pfd = {pipes[i]->readEnd(), POLLIN, 0};
        ASSERT_EQ(1, ::poll(&pfd, 1, 1000)) << "pipe " << i << " should still hold its unread byte";

        char buf = 0;
        EXPECT_EQ(1, ::read(pipes[i]->readEnd(), &buf, 1));
    }

    ASSERT_NO_FATAL_FAILURE(runRound({0, 11, 25}));
}

// Descriptor numbers are recycled by the kernel. A missed mIndexByFd reset lets
// a recycled number read a stale entry's revents.
TEST(FdWatchSet, DescriptorNumberReuse)
{
    FdWatchSet set;

    int recycled = -1;
    {
        Pipe first;
        recycled = first.readEnd();

        set.clear();
        set.watchReadable(first.readEnd(), ExecOnReady::Yes);
        first.writeByte();
        ASSERT_GT(set.wait(1000), 0);
        ASSERT_TRUE(set.isReadable(first.readEnd()));
    } // first's descriptors are closed here

    // Before clear() the set still answers from the previous cycle's entry for
    // this descriptor NUMBER. Not a defect to fix: nothing short of tracking
    // file descriptions could tell the number now means a different pipe, which
    // is exactly why clear() is mandatory. Asserted so the guarantee below is a
    // real before/after pair rather than a claim about an untested state.
    Pipe second;
    if (second.readEnd() == recycled)
    {
        EXPECT_TRUE(set.isReadable(second.readEnd()))
            << "a stale entry is expected here; clear() is what removes it";
    }

    // After clear(): registering the recycled number afresh must not inherit
    // anything.
    set.clear();
    set.watchReadable(second.readEnd(), ExecOnReady::Yes);

    EXPECT_EQ(0, set.wait(50));
    EXPECT_FALSE(set.isReadable(second.readEnd()));
}

TEST(FdWatchSet, ExecOnReadyNoIsNotExec)
{
    Pipe p;
    FdWatchSet set;

    set.clear();
    set.watchReadable(p.readEnd(), ExecOnReady::No);
    p.writeByte();

    ASSERT_GT(set.wait(1000), 0);
    EXPECT_TRUE(set.isReadable(p.readEnd())) << "still reported as ready";
    EXPECT_FALSE(set.anyReadyNeedsExec()) << "but its registrant interprets it";
}

TEST(FdWatchSet, ExecOnReadyYesIsExec)
{
    Pipe p;
    FdWatchSet set;

    set.clear();
    set.watchReadable(p.readEnd(), ExecOnReady::Yes);
    p.writeByte();

    ASSERT_GT(set.wait(1000), 0);
    EXPECT_TRUE(set.anyReadyNeedsExec());
}

// The permanent-spin regression: whoever owns the dead descriptor has to be told
// about it, or nothing unregisters it and wait() returns instantly forever.
TEST(FdWatchSet, StaleDescriptorIsReported)
{
    Pipe healthy; // open and empty, so nothing can make it readable on its own
    FdWatchSet set;

    int stale = -1;
    {
        Pipe p;
        stale = p.readEnd();
        set.clear();
        set.watchReadable(healthy.readEnd(), ExecOnReady::No);
        // ExecOnReady::No on purpose: a broken descriptor must wake exec()
        // regardless of who registered it.
        set.watchReadable(stale, ExecOnReady::No);
    } // both ends closed here, so `stale` is now an invalid descriptor

    const int n = set.wait(0);

    // One set of assertions for both backends: poll() flags POLLNVAL itself,
    // and the select() backend probes each descriptor to reach the same place.
    EXPECT_NE(0, n) << "a stale descriptor is not a timeout";
    EXPECT_TRUE(set.isReadable(stale)) << "the owner is never told the descriptor died";
    EXPECT_FALSE(set.isReadable(healthy.readEnd()))
        << "readiness invented for a live descriptor; its owner may block reading it";
    EXPECT_TRUE(set.anyReadyNeedsExec()) << "POLLNVAL is in Broken";
}

// PosixWaiter's constructor asks isWatchable() before committing to a wakeup
// pipe, so its answer is interface, not an implementation detail of watch().
TEST(FdWatchSet, IsWatchableReportsTheBackendCeiling)
{
    Pipe p;
    EXPECT_TRUE(FdWatchSet::isWatchable(p.readEnd()));

    // A failed open() must not read as watchable.
    EXPECT_FALSE(FdWatchSet::isWatchable(-1));

#ifdef USE_POLL
    EXPECT_TRUE(FdWatchSet::isWatchable(FD_SETSIZE)) << "poll() has no descriptor-number ceiling";
#else
    EXPECT_FALSE(FdWatchSet::isWatchable(FD_SETSIZE))
        << "the select() backend cannot watch descriptors at or above FD_SETSIZE";
#endif
}

// The other half of the ceiling: a false from isWatchable() has to make watch()
// decline. Registering anyway would abort inside FD_SET, the point of all this.
TEST(FdWatchSet, WatchDeclinesDescriptorsAboveFdSetSize)
{
#ifdef USE_POLL
    GTEST_SKIP() << "poll() accepts any descriptor number, so there is no refusal path";
#else
    // dup2 onto a chosen number rather than opening FD_SETSIZE descriptors: the
    // refusal depends on the number alone, so only the limit has to be raised.
    constexpr int High = FD_SETSIZE + 8;

    std::unique_ptr<RlimitRestorer> restorer;
    if (const std::string why = ensureNofileAtLeast(High + 1, restorer); !why.empty())
    {
        GTEST_SKIP() << why;
    }

    Pipe p;
    // The only assertion before the close() below, so nothing can leak `High`.
    ASSERT_EQ(High, ::dup2(p.readEnd(), High)) << "dup2 onto " << High << " failed: " << errno;

    FdWatchSet set;
    set.clear();
    set.watchReadable(High, ExecOnReady::Yes);

    p.writeByte(); // High would be readable now, had it been registered

    // Nothing else can make wait() report zero here. Were the descriptor
    // registered, the backend's assert(d.fd < FD_SETSIZE) would fire first, so a
    // regression surfaces as that assert rather than as this expectation.
    EXPECT_EQ(0, set.wait(0)) << "a refused descriptor must leave nothing registered";
    EXPECT_FALSE(set.isReadable(High)) << "a refused descriptor is never reported ready";
    EXPECT_FALSE(set.anyReadyNeedsExec());

    ::close(High);
#endif
}

// This ticket's core regression: descriptors at or above FD_SETSIZE, where
// glibc's __fdelt_chk aborts. The select() backend refuses them instead, so the
// assertions differ and this test is poll()-only.
TEST(FdWatchSet, ManyDescriptorsAboveFdSetSize)
{
#ifndef USE_POLL
    GTEST_SKIP() << "the select() backend refuses descriptors >= FD_SETSIZE by "
                    "design; see FdWatchSet::canWatch()";
#else
    // 600 pipes is 1200 descriptors, comfortably past FD_SETSIZE (1024).
    constexpr size_t PipeCount = 600;
    constexpr rlim_t Needed = 2 * PipeCount + 64; // + headroom for gtest's own

    std::unique_ptr<RlimitRestorer> restorer;
    if (const std::string why = ensureNofileAtLeast(Needed, restorer); !why.empty())
    {
        GTEST_SKIP() << why;
    }

    std::vector<std::unique_ptr<Pipe>> pipes;
    for (size_t i = 0; i < PipeCount; ++i)
    {
        pipes.push_back(std::make_unique<Pipe>());
    }

    // Confirm we actually got past the ceiling, otherwise the test proves
    // nothing.
    int highest = -1;
    for (const auto& p: pipes)
    {
        highest = std::max(highest, std::max(p->readEnd(), p->writeEnd()));
    }
    ASSERT_GE(highest, FD_SETSIZE) << "highest descriptor was only " << highest
                                   << "; this test needs one at or above " << FD_SETSIZE;

    FdWatchSet set;
    set.clear();
    for (const auto& p: pipes)
    {
        set.watchReadable(p->readEnd(), ExecOnReady::Yes);
    }

    pipes.back()->writeByte();

    ASSERT_GT(set.wait(1000), 0);

    // Exactly one readable, and it is the one we wrote to.
    size_t readableCount = 0;
    for (const auto& p: pipes)
    {
        if (set.isReadable(p->readEnd()))
        {
            ++readableCount;
            EXPECT_EQ(pipes.back()->readEnd(), p->readEnd());
        }
    }
    EXPECT_EQ(1u, readableCount);
#endif
}
