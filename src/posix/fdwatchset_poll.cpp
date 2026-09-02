/**
 * @file posix/fdwatchset_poll.cpp
 * @brief POSIX descriptor readiness, poll() backend
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

// Package.swift compiles every source file instead of letting CMake pick one
// backend, so this guard is what keeps exactly one definition of these symbols.
// The select() backend takes the other side of the condition.
#ifdef USE_POLL

#include "mega/posix/megafdwatchset.h"

namespace mega
{

bool FdWatchSet::isWatchable(int fd)
{
    // poll() has no descriptor-number ceiling, so only a negative fd is refused.
    return fd >= 0;
}

bool FdWatchSet::canWatch(int fd)
{
    return isWatchable(fd);
}

int FdWatchSet::wait(int timeoutMs)
{
    onWaitEntry();

    return poll(mDescriptors.data(), static_cast<nfds_t>(mDescriptors.size()), timeoutMs);
}

} // namespace mega

#endif // USE_POLL
