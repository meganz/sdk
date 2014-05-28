/**
 * @file posix/wait.cpp
 * @brief POSIX event/timeout handling
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

#ifdef __APPLE__
#define CLOCK_MONOTONIC 0
int clock_gettime(int, struct timespec* t)
{
    struct timeval now;
    int rv = gettimeofday(&now, NULL);
    if (rv)
    {
        return rv;
    }
    t->tv_sec = now.tv_sec;
    t->tv_nsec = now.tv_usec * 1000;
    return 0;
}
#endif

namespace mega {
dstime Waiter::ds;

void PosixWaiter::init(dstime ds)
{
    Waiter::init(ds);

    maxfd = -1;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    FD_ZERO(&ignorefds);
}

// update monotonously increasing timestamp in deciseconds
void Waiter::bumpds()
{
    timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    ds = ts.tv_sec * 10 + ts.tv_nsec / 100000000;
}

// update maxfd for select()
void PosixWaiter::bumpmaxfd(int fd)
{
    if (fd > maxfd)
    {
        maxfd = fd;
    }
}

// checks if an unfiltered fd is set
// FIXME: use bitwise & instead of scanning
bool PosixWaiter::fd_filter(int nfds, fd_set* fds, fd_set* ignorefds) const
{
    while (nfds--)
    {
        if (FD_ISSET(nfds, fds) && !FD_ISSET(nfds, ignorefds)) return true;    
    }

    return false;
}

// wait for supplied events (sockets, filesystem changes), plus timeout + application events
// maxds specifies the maximum amount of time to wait in deciseconds (or ~0 if no timeout scheduled)
// returns application-specific bitmask. bit 0 set indicates that exec() needs to be called.
int PosixWaiter::wait()
{
    int numfd;
    timeval tv;

    if (maxds + 1)
    {
        dstime us = 1000000 / 10 * maxds;

        tv.tv_sec = us / 1000000;
        tv.tv_usec = us - tv.tv_sec * 1000000;
    }

    numfd = select(maxfd + 1, &rfds, &wfds, &efds, maxds + 1 ? &tv : NULL);

    // timeout or error
    if (numfd <= 0)
    {
        return NEEDEXEC;
    }

    // request exec() to be run only if a non-ignored fd was triggered
    return (fd_filter(maxfd + 1, &rfds, &ignorefds)
         || fd_filter(maxfd + 1, &wfds, &ignorefds)
         || fd_filter(maxfd + 1, &efds, &ignorefds)) ? NEEDEXEC : 0;
}
} // namespace
