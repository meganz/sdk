/**
 * @file posix/fdwatchset.cpp
 * @brief POSIX descriptor registration and readiness, backend-independent part
 *
 * (c) 2026 by Mega Limited, Auckland, New Zealand
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

#include "mega/posix/megafdwatchset.h"

#include <cstddef>

namespace mega
{

void FdWatchSet::clear()
{
    // Resets only the slots in use and keeps mIndexByFd's capacity: rebuilding
    // that table every cycle is the cost this structure exists to avoid.
    for (const struct pollfd& d: mDescriptors)
    {
        mIndexByFd[static_cast<size_t>(d.fd)] = -1;
    }

    mDescriptors.clear();
    mExecOnReady.clear();
    mClearedSinceWait = true;

    // mOverflowLogged is deliberately NOT reset: fd >= FD_SETSIZE is a build
    // configuration problem, not a transient one.
}

void FdWatchSet::watch(int fd, short events, ExecOnReady e)
{
    assert(fd >= 0);

    // Kept here rather than left to canWatch(): under NDEBUG a negative fd
    // casts to SIZE_MAX, so resize() wraps to zero -- dropping every mapping
    // made this cycle -- and the index write lands out of bounds.
    if (fd < 0 || !canWatch(fd))
    {
        return;
    }

    const auto ufd = static_cast<size_t>(fd);
    if (mIndexByFd.size() <= ufd)
    {
        mIndexByFd.resize(ufd + 1, -1);
    }

    int& idx = mIndexByFd[ufd];
    if (idx < 0)
    {
        idx = static_cast<int>(mDescriptors.size());
        mDescriptors.push_back({fd, events, 0});
        mExecOnReady.push_back(e == ExecOnReady::Yes ? 1 : 0);
    }
    else
    {
        struct pollfd& d = mDescriptors[static_cast<size_t>(idx)];
        d.events = static_cast<short>(d.events | events);

        // If either registration wants exec(), run exec(): one extra exec() is
        // cheap, a missing one stalls the loop.
        if (e == ExecOnReady::Yes)
        {
            mExecOnReady[static_cast<size_t>(idx)] = 1;
        }
    }
}

const struct pollfd* FdWatchSet::find(int fd) const
{
    if (fd < 0)
    {
        return nullptr;
    }

    const auto ufd = static_cast<size_t>(fd);
    if (ufd >= mIndexByFd.size())
    {
        return nullptr;
    }

    const int idx = mIndexByFd[ufd];

    return idx < 0 ? nullptr : &mDescriptors[static_cast<size_t>(idx)];
}

bool FdWatchSet::anyReadyNeedsExec() const
{
    for (size_t i = 0; i < mDescriptors.size(); ++i)
    {
        if (!mDescriptors[i].revents)
        {
            continue;
        }

        // Broken ignores ExecOnReady: nobody registered for this state, so
        // exec() must get a chance at it. All three bits -- a POLLHUP alone
        // leaves poll() returning instantly with nobody asking for exec().
        if (mDescriptors[i].revents & Broken)
        {
            return true;
        }

        if (mExecOnReady[i])
        {
            return true;
        }
    }

    return false;
}

void FdWatchSet::onWaitEntry()
{
    // The rebuild invariant: clear() between any two wait() calls. Violating it
    // accumulates registrations, so a descriptor that has gone away is polled
    // forever -- silent and cumulative, hence an assert. It also catches a
    // caller that borrowed another component's waiter mid-cycle.
    assert(mClearedSinceWait && "FdWatchSet::wait() must not reuse the previous cycle's "
                                "registrations -- call clear() first");
    mClearedSinceWait = false;
}

} // namespace mega
