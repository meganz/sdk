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


#ifdef USE_POLL
    #include <poll.h> //poll
#endif

namespace mega {

PosixWaiter::PosixWaiter()
{
    // pipe to be able to leave the select() call
    if (pipe(m_pipe) < 0)
    {
        LOG_fatal << "Error creating pipe";
        throw std::runtime_error("Error creating pipe");
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

    MEGA_FD_ZERO(&rfds);
    MEGA_FD_ZERO(&wfds);
    MEGA_FD_ZERO(&efds);
    MEGA_FD_ZERO(&ignorefds);
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
bool PosixWaiter::fd_filter(int nfds, mega_fd_set_t* fds, mega_fd_set_t* ignorefds) const
{
    while (nfds--)
    {
        if (MEGA_FD_ISSET(nfds, fds) && !MEGA_FD_ISSET(nfds, ignorefds)) return true;
    }

    return false;
}

// wait for supplied events (sockets, filesystem changes), plus timeout + application events
// maxds specifies the maximum amount of time to wait in deciseconds (or ~0 if no timeout scheduled)
// returns application-specific bitmask. bit 0 set indicates that exec() needs to be called.
int PosixWaiter::wait()
{
    int numfd = 0;
    timeval tv;

    //Pipe added to rfds to be able to leave select() when needed
    MEGA_FD_SET(m_pipe[0], &rfds);

    bumpmaxfd(m_pipe[0]);

    if (maxds + 1)
    {
        dstime us = 1000000 / 10 * maxds;

        tv.tv_sec = us / 1000000;
        tv.tv_usec = (suseconds_t)(us - tv.tv_sec * 1000000);
    }

#ifdef USE_POLL
    // wait infinite (-1) if maxds is max dstime OR it would overflow platform's int
    int timeoutInMs = -1;
    if (maxds != std::numeric_limits<dstime>::max() &&
        maxds <= std::numeric_limits<int>::max() / 100)
    {
        timeoutInMs = static_cast<int>(maxds) * 100;
    }
    auto total = rfds.size() + wfds.size() + efds.size();
    struct pollfd fds[total];

    int polli = 0;
    for (auto & fd : rfds)
    {
        fds[polli].fd = fd;
        fds[polli].events = POLLIN_SET;
        polli++;
    }

    for (auto & fd : wfds)
    {
        fds[polli].fd = fd;
        fds[polli].events = POLLOUT_SET;
        polli++;
    }

    for (auto & fd : efds)
    {
        fds[polli].fd = fd;
        fds[polli].events = POLLEX_SET;
        polli++;
    }
    numfd = poll(fds, total, timeoutInMs);
#else
    numfd = select(maxfd + 1, &rfds, &wfds, &efds, maxds + 1 ? &tv : NULL);
#endif

    // empty pipe
    uint8_t buf;
    bool external = false;

    {
        std::lock_guard<std::mutex> g(mMutex);
        while (read(m_pipe[0], &buf, sizeof buf) > 0)
        {
            external = true;
        }
        alreadyNotified = false;
    }

    // timeout or error
    if (external || numfd <= 0)
    {
        return NEEDEXEC;
    }

    // request exec() to be run only if a non-ignored fd was triggered
#ifdef USE_POLL
    for (unsigned int i = 0 ; i < total ; i++)
    {
        if  ((fds[i].revents & (POLLIN_SET | POLLOUT_SET | POLLEX_SET) )  && !MEGA_FD_ISSET(fds[i].fd, &ignorefds) )
        {
            return NEEDEXEC;
        }
    }
    return 0;
#else
    return (fd_filter(maxfd + 1, &rfds, &ignorefds)
         || fd_filter(maxfd + 1, &wfds, &ignorefds)
         || fd_filter(maxfd + 1, &efds, &ignorefds)) ? NEEDEXEC : 0;

#endif
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
} // namespace
