/**
 * @file mega/win32/megaconsolewaiter.h
 * @brief Win32 event/timeout handling, listens for console input
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

#ifndef CONSOLE_WAIT_CLASS
#define CONSOLE_WAIT_CLASS WinConsoleWaiter

namespace mega {

class MEGA_API WinConsoleWaiter : public WinWaiter
{
#ifdef NO_READLINE
    WinConsole* console;
#else
    HANDLE hInput;
#endif


public:
    int wait();

    WinConsoleWaiter(WinConsole*);
};

} // namespace

#endif
