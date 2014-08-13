/**
 * @file win32/wait.cpp
 * @brief Win32 event/timeout handling
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
#include "mega/wp8/megawaiter.h"

#include <thread>

namespace mega {
dstime Waiter::ds;

typedef ULONGLONG (WINAPI * PGTC)();

static PGTC pGTC;
static ULONGLONG tickhigh;
static DWORD prevt;

WinPhoneWaiter::WinPhoneWaiter()
{
    //if (!pGTC) pGTC = (PGTC)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetTickCount64");

    if (!pGTC)
    {
        tickhigh = 0;
        prevt = 0;
    }
	maxfd = -1;
	notified = false;
}

void WinPhoneWaiter::init(dstime ds)
{
	Waiter::init(ds);

	maxfd = -1;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);
	FD_ZERO(&ignorefds);
}

// update monotonously increasing timestamp in deciseconds
// FIXME: restore thread safety for applications using multiple MegaClient objects
void WinPhoneWaiter::bumpds()
{
    if (pGTC)
    {
        ds = pGTC() / 100;
    }
    else
    {
        // emulate GetTickCount64() on XP
        DWORD t = GetTickCount64();

        if (t < prevt)
        {
            tickhigh += 0x100000000;
        }

        prevt = t;

        ds = (t + tickhigh) / 100;
    }
}

// update maxfd for select()
void WinPhoneWaiter::bumpmaxfd(int fd)
{
	if (fd > maxfd)
	{
		maxfd = fd;
	}
}

// checks if an unfiltered fd is set
// FIXME: use bitwise & instead of scanning
bool WinPhoneWaiter::fd_filter(int nfds, fd_set* fds, fd_set* ignorefds) const
{
	while (nfds--)
	{
		if (FD_ISSET(nfds, fds) && !FD_ISSET(nfds, ignorefds)) return true;
	}

	return false;
}

// wait for events (socket, I/O completion, timeout + application events)
// ds specifies the maximum amount of time to wait in deciseconds (or ~0 if no
// timeout scheduled)
// (this assumes that the second call to addhandle() was coming from the
// network layer)
int WinPhoneWaiter::wait()
{
	timeval tv;
	if (maxds < 0 || maxds > 10)
		maxds = 10;

	dstime us = 1000000 / 10 * maxds;
	tv.tv_sec = us / 1000000;
	tv.tv_usec = us - tv.tv_sec * 1000000;

	if (maxfd >= 0)
	{
		select(maxfd + 1, &rfds, &wfds, &efds, &tv);
		return NEEDEXEC;
	}
	else
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		if (notified)
		{
			notified = false;
			return NEEDEXEC;
		}

		//Needed because cURL doesn't include c-ares in our build yet :-/
		return NEEDEXEC;
	}
}

// add handle to the list - must not be called twice with the same handle
// return true if handle added
bool WinPhoneWaiter::addhandle(HANDLE handle, int flag)
{
    handles.push_back(handle);
    flags.push_back(flag);

    return true;
}

void WinPhoneWaiter::notify()
{
	notified = true;
}

} // namespace
