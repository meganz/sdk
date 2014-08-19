/**
 * @file mega/win32/megawaiter.h
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

#ifndef WAIT_CLASS
#define WAIT_CLASS WinPhoneWaiter

namespace mega {
class MEGA_API WinPhoneWaiter : public Waiter
{
    vector<HANDLE> handles;
    vector<int> flags;
	int maxfd;
	fd_set ignorefds;

	bool fd_filter(int nfds, fd_set* fds, fd_set* ignorefds) const;
	bool notified;

public:
    PCRITICAL_SECTION pcsHTTP;
    unsigned pendingfsevents;

    int wait();
	static void bumpds();
	void init(dstime ds);
	void bumpmaxfd(int);
	fd_set rfds, wfds, efds;

    bool addhandle(HANDLE handle, int);

	WinPhoneWaiter();

	void notify();
};
} // namespace

#endif
