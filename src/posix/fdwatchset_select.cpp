/**
 * @file posix/fdwatchset_select.cpp
 * @brief POSIX descriptor readiness, select() backend
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

// See fdwatchset_poll.cpp: exactly one backend compiles, Package.swift
// included. Undefined USE_POLL means select(), the mapping CMake uses too.
#ifndef USE_POLL

#include "mega/logging.h"
#include "mega/posix/megafdwatchset.h"

#include <sys/select.h>
#include <sys/time.h>

#include <cerrno>
#include <fcntl.h>

namespace mega
{

bool FdWatchSet::isWatchable(int fd)
{
    return fd >= 0 && fd < FD_SETSIZE;
}

bool FdWatchSet::canWatch(int fd)
{
    if (isWatchable(fd))
    {
        return true;
    }

    // Registering it would abort the process later inside FD_SET, through
    // glibc's __fdelt_chk, killing the session and every in-flight transfer.
    // Refusing leaves the descriptor unmonitored instead, which most callers
    // survive degraded. The two that cannot -- the wakeup pipe and the udev
    // monitor -- ask isWatchable() up front rather than reach this path.
    if (!mOverflowLogged)
    {
        mOverflowLogged = true;
        LOG_err << "Descriptor " << fd << " is at or above FD_SETSIZE (" << FD_SETSIZE
                << ") and cannot be watched by the select() backend. "
                << "Rebuild with -DUSE_POLL=ON.";
    }

    return false;
}

int FdWatchSet::wait(int timeoutMs)
{
    onWaitEntry();

    // Built fresh each cycle, so select() clobbering them in place is harmless
    // and no registration/working-copy pair is needed.
    fd_set readReady;
    fd_set writeReady;
    FD_ZERO(&readReady);
    FD_ZERO(&writeReady);
    int maxFd = -1;

    for (struct pollfd& d: mDescriptors)
    {
        d.revents = 0;

        // canWatch() already refused fd >= FD_SETSIZE, so FD_SET is safe here.
        assert(d.fd < FD_SETSIZE);

        if (d.events & POLLIN)
        {
            FD_SET(d.fd, &readReady);
        }
        if (d.events & POLLOUT)
        {
            FD_SET(d.fd, &writeReady);
        }
        if (d.fd > maxFd)
        {
            maxFd = d.fd;
        }
    }

    int n;
    if (timeoutMs < 0)
    {
        n = select(maxFd + 1, &readReady, &writeReady, nullptr, nullptr);
    }
    else
    {
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);
        n = select(maxFd + 1, &readReady, &writeReady, nullptr, &tv);
    }

    if (n < 0)
    {
        const int failure = errno; // the probe below overwrites it

        // Probed on every failure rather than gated on EBADF: which errno
        // implies a bad descriptor is the kernel's business. Reporting them all
        // instead invents readiness for stdin's owner, which reads it blocking.
        for (struct pollfd& d: mDescriptors)
        {
            if (fcntl(d.fd, F_GETFD) == -1 && errno == EBADF)
            {
                d.revents = static_cast<short>(POLLNVAL);
            }
        }

        errno = failure;

        return n;
    }

    if (n == 0)
    {
        return n;
    }

    // Fold the in-place result back into revents, so the readiness accessors are
    // backend-independent; filtering by events keeps contract point 4. The
    // Broken bits stay clear because select() cannot tell an error from data --
    // both arrive as plain readability.
    for (struct pollfd& d: mDescriptors)
    {
        if ((d.events & POLLIN) && FD_ISSET(d.fd, &readReady))
        {
            d.revents = static_cast<short>(d.revents | POLLIN);
        }
        if ((d.events & POLLOUT) && FD_ISSET(d.fd, &writeReady))
        {
            d.revents = static_cast<short>(d.revents | POLLOUT);
        }
    }

    return n;
}

} // namespace mega

#endif // !USE_POLL
