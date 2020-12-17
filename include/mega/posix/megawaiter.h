/**
 * @file mega/posix/megawaiter.h
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

#ifndef WAIT_CLASS
#define WAIT_CLASS PosixWaiter

#include "mega/waiter.h"
#include <mutex>

#ifndef USE_POLL
    #define MEGA_FD_ZERO FD_ZERO
    #define MEGA_FD_SET FD_SET
    #define MEGA_FD_ISSET FD_ISSET
    typedef fd_set mega_fd_set_t;
#else

    #define MEGA_FD_ZERO PosixWaiter::clear_fdset
    #define MEGA_FD_SET PosixWaiter::fdset
    #define MEGA_FD_ISSET PosixWaiter::fdisset

    #define POLLIN_SET  (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR) // Ready for reading
    #define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR) // Ready for writing
    #define POLLEX_SET  (POLLPRI) // Exceptional condition
    typedef std::set<int> mega_fd_set_t ;

#endif

namespace mega {
struct PosixWaiter : public Waiter
{
    PosixWaiter();
    ~PosixWaiter();

    int maxfd;

    mega_fd_set_t rfds, wfds, efds;
    mega_fd_set_t ignorefds;

#ifdef USE_POLL

    static void clear_fdset(mega_fd_set_t *s)
    {
        s->clear();
    }
    static void fdset(int fd, mega_fd_set_t *s)
    {
        s->insert(fd);
    }
    static bool fdisset(int fd, mega_fd_set_t *s)
    {
        return s->find(fd) != s->end();
    }
#endif

    bool fd_filter(int nfds, mega_fd_set_t* fds, mega_fd_set_t* ignorefds) const;

    void init(dstime);
    int wait();
    void bumpmaxfd(int);

    void notify();

protected:
    int m_pipe[2];
    std::mutex mMutex;
    bool alreadyNotified = false;
};
} // namespace

#endif
