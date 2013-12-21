/**
 * @file posix/wait.cpp
 * @brief POSIX event/timeout handling
 *
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
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

void PosixWaiter::init(dstime ds)
{
	maxds = ds;

	maxfd = -1;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);
}

// update monotonously increasing timestamp in deciseconds
dstime PosixWaiter::getdstime()
{
	timespec ts;

	clock_gettime(CLOCK_MONOTONIC,&ts);

	return ds = ts.tv_sec*10+ts.tv_nsec/100000000;
}

// update maxfd for select()
void PosixWaiter::bumpmaxfd(int fd)
{
	if (fd > maxfd) maxfd = fd;
}

// monitor file descriptors
// return value from select ()
int PosixWaiter::monitor_fds ()
{
	timeval tv;

	if (maxds+1)
	{
		dstime us = 1000000/10*(maxds + 1);

		tv.tv_sec = us/1000000;
		tv.tv_usec = us-tv.tv_sec*1000000;

    // special case when there are no pending events
    } else {
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
    }

	return select(maxfd+1,&rfds,&wfds,&efds,&tv);
}

// wait for supplied events (sockets, filesystem changes), plus timeout + application events
// maxds specifies the maximum amount of time to wait in deciseconds (or ~0 if no timeout scheduled)
// returns application-specific bitmask. bit 0 set indicates that exec() needs to be called.
int PosixWaiter::wait()
{
    int numfd;

    numfd = monitor_fds ();

	// timeout or error
	if (numfd <= 0) return NEEDEXEC;

	return NEEDEXEC;
}

} // namespace
