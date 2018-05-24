/**
 * @file win32/consolewaiter.cpp
 * @brief Win32 event/timeout handling, listens for console input
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
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
#include "megaconsolewaiter.h"
#include "megaconsole.h"

namespace mega {
WinConsoleWaiter::WinConsoleWaiter(WinConsole* con)
    : console(con)
{
}

// wait for events (socket, I/O completion, timeout + application events)
// ds specifies the maximum amount of time to wait in deciseconds (or ~0 if no
// timeout scheduled)
int WinConsoleWaiter::wait()
{
    int r;

    if (console)
    {

        addhandle(console->inputAvailableHandle(), 0);
    }

    // aggregated wait
    r = WinWaiter::wait();

    // is it a network- or filesystem-triggered wakeup?
    if (r)
    {
        return r;
    }

    if (console && console->consolePeek())
    {
        return HAVESTDIN;
    }
    return 0;
}
} // namespace
