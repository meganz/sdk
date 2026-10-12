/**
 * @file posix/wait.cpp
 * @brief POSIX event/timeout handling
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega.h"

#include <limits>

namespace mega
{

PosixWaiter::PosixWaiter()
{
    // pipe to be able to leave the poll()/select() call
    if (pipe(m_pipe) < 0)
    {
        LOG_fatal << "Error creating pipe";
        throw std::runtime_error("Error creating pipe");
    }

    // notify() only writes to m_pipe[1], so with m_pipe[0] unwatched nothing can
    // ever wake wait() and an idle client blocks on NEVER forever. Dead code
    // under poll(), which refuses only a negative fd -- ruled out just above.
    if (!FdWatchSet::isWatchable(m_pipe[0]))
    {
        LOG_fatal << "Wakeup pipe descriptor " << m_pipe[0]
                  << " cannot be watched by this build's waiter backend; rebuild with "
                     "-DUSE_POLL=ON";
        close(m_pipe[0]);
        close(m_pipe[1]);
        throw std::runtime_error("Wakeup pipe descriptor cannot be watched");
    }

    if (fcntl(m_pipe[0], F_SETFL, O_NONBLOCK) < 0)
    {
        LOG_err << "fcntl error";
    }
}

PosixWaiter::~PosixWaiter()
{
    close(m_pipe[0]);
    close(m_pipe[1]);
}

void PosixWaiter::init(dstime ds)
{
    Waiter::init(ds);

    mWatched.clear();
}

void PosixWaiter::watchReadable(int fd, ExecOnReady e)
{
    mWatched.watchReadable(fd, e);
}

void PosixWaiter::watchWritable(int fd, ExecOnReady e)
{
    mWatched.watchWritable(fd, e);
}

bool PosixWaiter::isReadable(int fd) const
{
    return mWatched.isReadable(fd);
}

bool PosixWaiter::isWritable(int fd) const
{
    return mWatched.isWritable(fd);
}

int PosixWaiter::timeoutMsFor(dstime ds)
{
    // Takes ds by value: maxds is atomic, and reading it twice -- once for
    // EVER(), once for the conversion -- let another thread change it between.
    if (!EVER(ds))
    {
        return -1; // wait forever
    }

    if (ds <= 0)
    {
        // The deadline has already passed. Returning a negative value would
        // mean "wait forever" instead.
        return 0;
    }

    // dstime is int64_t. Clamp to int rather than degrading into an infinite
    // wait: a large but finite ceiling means a long wait, not a permanent one.
    constexpr dstime cap = std::numeric_limits<int>::max() / 100;

    return ds > cap ? std::numeric_limits<int>::max() : static_cast<int>(ds) * 100;
}

bool PosixWaiter::drainPipe()
{
    uint8_t buf;
    bool external = false;

    std::lock_guard<std::mutex> g(mMutex);
    while (read(m_pipe[0], &buf, sizeof buf) > 0)
    {
        external = true;
    }
    alreadyNotified = false;

    return external;
}

// wait for supplied events (sockets, filesystem changes), plus timeout and
// application events. Returns an application-specific bitmask; bit 0
// (NEEDEXEC) set indicates that exec() needs to be called.
int PosixWaiter::wait()
{
    // Pipe readiness is interpreted right here by drainPipe(), so it must not
    // request NEEDEXEC on its own.
    mWatched.watchReadable(m_pipe[0], ExecOnReady::No);

    const int n = mWatched.wait(timeoutMsFor(maxds.load()));
    const bool external = drainPipe();

    // timeout, error, or an external wakeup
    if (external || n <= 0)
    {
        return NEEDEXEC;
    }

    return mWatched.anyReadyNeedsExec() ? NEEDEXEC : 0;
}

void PosixWaiter::notify()
{
    std::lock_guard<std::mutex> g(mMutex);
    if (!alreadyNotified)
    {
        auto w = write(m_pipe[1], "0", 1);
        if (w <= 0)
        {
            LOG_warn << "PosixWaiter::notify(), write returned " << w;
        }
        alreadyNotified = true;
    }
}
} // namespace mega
