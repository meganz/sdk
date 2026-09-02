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

#include "mega/posix/megafdwatchset.h"
#include "mega/waiter.h"

#include <mutex>

namespace mega
{
struct PosixWaiter: public Waiter
{
    PosixWaiter();
    ~PosixWaiter();

    void init(dstime) override;
    int wait() override;
    void notify() override;

    // Register a descriptor for this wait cycle. ExecOnReady::No means the
    // registrant interprets readiness itself and does not want NEEDEXEC
    // requested on its behalf.
    void watchReadable(int fd, ExecOnReady e = ExecOnReady::Yes);
    void watchWritable(int fd, ExecOnReady e = ExecOnReady::Yes);

    // Readiness, not registration, and only for the direction registered.
    // Meaningful after wait() returns.
    bool isReadable(int fd) const;
    bool isWritable(int fd) const;

    // Deciseconds to a poll()/select() timeout in milliseconds. Static and
    // public so the clamping is directly testable.
    static int timeoutMsFor(dstime ds);

private:
    // Drain the wakeup pipe. Returns true if notify() had been called.
    bool drainPipe();

    FdWatchSet mWatched;

    int m_pipe[2];
    std::mutex mMutex;
    bool alreadyNotified = false;
};
} // namespace mega

#endif
