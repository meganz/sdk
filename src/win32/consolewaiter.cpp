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
#ifdef NO_READLINE
    : console(con)
#endif
{
#ifndef NO_READLINE
    DWORD dwMode;

    hInput = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(hInput, &dwMode);
    SetConsoleMode(hInput, dwMode & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
    FlushConsoleInputBuffer(hInput);
#endif
}

// wait for events (socket, I/O completion, timeout + application events)
// ds specifies the maximum amount of time to wait in deciseconds (or ~0 if no
// timeout scheduled)
int WinConsoleWaiter::wait()
{
    int r;
#ifdef NO_READLINE

    if (console)
    {

        addhandle(console->inputAvailableHandle(), 0);
    }
#else
    addhandle(hInput, 0);
#endif

    // aggregated wait
    r = WinWaiter::wait();

    // is it a network- or filesystem-triggered wakeup?
    if (r)
    {
        return r;
    }

#ifdef NO_READLINE
    if (console && console->consolePeek())
    {
        return HAVESTDIN;
    }
#else
    // FIXME: improve this gruesome nonblocking console read-simulating kludge
    if (_kbhit())
    {
        return HAVESTDIN;
    }

    // this assumes that the user isn't typing too fast
    INPUT_RECORD ir[1024];
    DWORD dwNum;
    ReadConsoleInput(hInput, ir, 1024, &dwNum);
#endif
    return 0;
}
} // namespace
