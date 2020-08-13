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

namespace mega {
dstime Waiter::ds;

PosixWaiter::PosixWaiter()
{
    for (;;)
    {
        // pipe to be able to leave the select() call
        if (pipe(m_pipe) < 0)
        {
            LOG_fatal << "Error creating pipe";
            throw std::runtime_error("Error creating pipe");
        }

        // On MacOS, pipe() keeps giving us 3 as the first fd.  Possibly on the first pipe() call per thread?
        if (m_pipe[0] > 3 && m_pipe[1] > 3) break;

        if (m_pipe[0] > 3) close(m_pipe[0]);
        if (m_pipe[1] > 3) close(m_pipe[1]);
    }

    if (fcntl(m_pipe[0], F_SETFL, O_NONBLOCK) < 0)
    {
        LOG_err << "fcntl error";
    }

    maxfd = -1;
}

PosixWaiter::~PosixWaiter()
{
    close(m_pipe[0]);
    close(m_pipe[1]);
}

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

    m_clock_getmonotonictime(&ts);

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

    //Pipe added to rfds to be able to leave select() when needed
    FD_SET(m_pipe[0], &rfds);
    bumpmaxfd(m_pipe[0]);

    if (maxds + 1)
    {
        dstime us = 1000000 / 10 * maxds;

        tv.tv_sec = us / 1000000;
        tv.tv_usec = us - tv.tv_sec * 1000000;
    }

    numfd = select(maxfd + 1, &rfds, &wfds, &efds, maxds + 1 ? &tv : NULL);

    // empty pipe
    uint8_t buf;
    bool external = false;

    while (mAtomicBytesSent > 0)
    {
        auto ret = read(m_pipe[0], &buf, 1);
        if (ret == 1)
        {
            --mAtomicBytesSent;
            external = true;
            continue;
        }
        break;
    }

    // timeout or error
    if (external || numfd <= 0)
    {
        return NEEDEXEC;
    }

    // request exec() to be run only if a non-ignored fd was triggered
    return (fd_filter(maxfd + 1, &rfds, &ignorefds)
         || fd_filter(maxfd + 1, &wfds, &ignorefds)
         || fd_filter(maxfd + 1, &efds, &ignorefds)) ? NEEDEXEC : 0;
}

void PosixWaiter::notify()
{
    if (mAtomicBytesSent == 0)
    {
        ++mAtomicBytesSent;
        write(m_pipe[1], "0", 1);
    }
}
} // namespace
