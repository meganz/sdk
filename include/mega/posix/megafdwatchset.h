/**
 * @file mega/posix/megafdwatchset.h
 * @brief POSIX descriptor registration and readiness
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

#ifndef MEGA_POSIX_FDWATCHSET_H
#define MEGA_POSIX_FDWATCHSET_H 1

#include <cassert>
#include <poll.h>
#include <vector>

namespace mega
{

// Whether a descriptor becoming ready, by itself, means exec() must run. No is
// for a registrant that interprets readiness itself in its own checkevents() or
// wait() override -- inotify, stdin, the wakeup pipe.
enum class ExecOnReady
{
    No,
    Yes
};

// Holds the descriptors a PosixWaiter is waiting on, and answers which of them
// became ready. USE_POLL picks the backend; no member below depends on it, so
// sizeof(FdWatchSet) does not either.
//
// The contract, in four points:
//
//  1. isReadable() / isWritable() answer READINESS, not registration.
//  2. Registrations SURVIVE wait(), unlike select()'s fd_sets. Structurally so:
//     registration is pollfd::events, readiness is pollfd::revents.
//  3. Readiness is meaningful after wait() returned > 0. An invalid descriptor
//     still reaches its owner, as POLLNVAL: poll() reports one on a successful
//     return, and select() can only fail, so that backend probes and reports.
//  4. Readiness is reported only for directions that were registered. Without
//     this the backends diverge, since poll() raises POLLERR/POLLHUP whatever
//     was requested while select() cannot report an fd absent from writefds.
//
// Not thread-safe, and does not need to be: one owning thread per instance.
// PosixWaiter::notify() is the one cross-thread entry point and touches nothing
// here.
class FdWatchSet
{
public:
    // Drop all registrations. Required between any two wait() calls -- see
    // onWaitEntry().
    void clear();

    // Register interest. The same fd in both directions is fine: it becomes one
    // entry with merged events.
    void watchReadable(int fd, ExecOnReady e)
    {
        watch(fd, POLLIN, e);
    }

    void watchWritable(int fd, ExecOnReady e)
    {
        watch(fd, POLLOUT, e);
    }

    // Block until a registered descriptor is ready or timeoutMs elapses;
    // timeoutMs < 0 waits forever. Returns the number of ready descriptors,
    // 0 on timeout, -1 on error with errno set. Backend-specific.
    int wait(int timeoutMs);

    bool isReadable(int fd) const
    {
        return isReady(fd, POLLIN);
    }

    bool isWritable(int fd) const
    {
        return isReady(fd, POLLOUT);
    }

    // A Broken descriptor counts here whatever its ExecOnReady -- see Broken.
    bool anyReadyNeedsExec() const;

    // False for a negative fd; select() also refuses fd >= FD_SETSIZE. Public
    // for an owner that cannot carry on once its descriptor is refused --
    // watch() declines the same ones, but asserts fd >= 0 first.
    static bool isWatchable(int fd);

private:
    // EOF and error conditions. select() counts them as ready, so we must too,
    // or cURL is never told its socket died and can only recover on timeout.
    static constexpr short Broken = static_cast<short>(POLLERR | POLLHUP | POLLNVAL);

    void watch(int fd, short events, ExecOnReady e);
    const struct pollfd* find(int fd) const;

    // Called first thing by each backend's wait(); asserts the rebuild
    // invariant.
    void onWaitEntry();

    // isWatchable(); the select() backend also logs a refusal once per set.
    bool canWatch(int fd);

    bool isReady(int fd, short direction) const
    {
        const struct pollfd* d = find(fd);
        // (d->events & direction) enforces contract point 4.
        return d && (d->events & direction) && (d->revents & (direction | Broken));
    }

    // One entry per watched fd; mIndexByFd maps fd -> index into it, -1 when
    // unwatched. The pair is a single invariant, never exposed.
    std::vector<struct pollfd> mDescriptors;
    // Sized to the highest fd seen, not the count watched, and clear() keeps its
    // capacity: memory traded for O(1) lookup, bounded by RLIMIT_NOFILE.
    std::vector<int> mIndexByFd;
    std::vector<char> mExecOnReady; // parallel to mDescriptors

    bool mOverflowLogged = false; // used by the select() backend only
    bool mClearedSinceWait = true; // guards the rebuild invariant
};

} // namespace mega

#endif // MEGA_POSIX_FDWATCHSET_H
