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

namespace mega {
dstime Waiter::ds;

int CALLBACK RejectFunc(LPWSABUF, LPWSABUF, LPQOS, LPQOS, LPWSABUF, LPWSABUF, GROUP FAR *, DWORD_PTR)
{ return CF_REJECT; }

WinPhoneWaiter::WinPhoneWaiter()
{
	maxfd = -1;

	int notifyAddressSize = sizeof(notifyAddress);
	wakupSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	notifyAddress.sin_family = AF_INET;
	notifyAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
	notifyAddress.sin_port = htons(0);
	::bind(wakupSocket, (SOCKADDR*)&notifyAddress, notifyAddressSize);
	::getsockname(wakupSocket, (SOCKADDR*)&notifyAddress, &notifyAddressSize);
	::listen(wakupSocket, SOMAXCONN);
}

void WinPhoneWaiter::init(dstime ds)
{
	Waiter::init(ds);

	maxfd = -1;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);
}

// update monotonously increasing timestamp in deciseconds
// FIXME: restore thread safety for applications using multiple MegaClient objects
void WinPhoneWaiter::bumpds()
{
	ds = GetTickCount64() / 100;
}

// update maxfd for select()
void WinPhoneWaiter::bumpmaxfd(int fd)
{
	if (fd > maxfd)
	{
		maxfd = fd;
	}
}

// wait for events (socket, I/O completion, timeout + application events)
// ds specifies the maximum amount of time to wait in deciseconds (or ~0 if no
// timeout scheduled)
int WinPhoneWaiter::wait()
{
	timeval tv;

	if (EVER(maxds))
	{
		dstime us = 1000000 / 10 * maxds;

		tv.tv_sec = us / 1000000;
		tv.tv_usec = us - tv.tv_sec * 1000000;
	}

	FD_SET(wakupSocket, &rfds);
	select(maxfd + 1, &rfds, &wfds, &efds, EVER(maxds) ? &tv : NULL);
	
	if (FD_ISSET(wakupSocket, &rfds))
		WSAAccept(wakupSocket, NULL, NULL, RejectFunc, NULL);

	return NEEDEXEC;
}

void WinPhoneWaiter::notify()
{
	u_long mode = 1;
	SOCKET notifySocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	::ioctlsocket(notifySocket, FIONBIO, &mode);
	::connect(notifySocket, (SOCKADDR*)&notifyAddress, sizeof(notifyAddress));
	::closesocket(notifySocket);
}

} // namespace
