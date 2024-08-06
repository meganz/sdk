/**
 * @file win32/wait.cpp
 * @brief Win32 event/timeout handling
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

#include "mega.h"
#include "megawaiter.h"

namespace mega {

WinWaiter::WinWaiter()
{
    externalEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

WinWaiter::~WinWaiter()
{
    CloseHandle(externalEvent);
}

// wait for events (socket, I/O completion, timeout + application events)
// ds specifies the maximum amount of time to wait in deciseconds (or ~0 if no
// timeout scheduled)
// (this assumes that the second call to addhandle() was coming from the
// network layer)
int WinWaiter::wait()
{
    int r = 0;
    addhandle(externalEvent, NEEDEXEC);

    if (index <= MAXIMUM_WAIT_OBJECTS)
    {
        assert(!handles.empty());
        DWORD dwWaitResult = WaitForMultipleObjectsEx(
            static_cast<DWORD>(index),
            &handles.front(),
            FALSE,
            (maxds > static_cast<dstime>(std::numeric_limits<DWORD>::max() / 100)) ?
                std::numeric_limits<DWORD>::max() :
                static_cast<DWORD>(maxds * 100),
            TRUE);

        assert(dwWaitResult != WAIT_FAILED);

#ifdef MEGA_MEASURE_CODE
        if (dwWaitResult == WAIT_TIMEOUT && maxds > 0) ++performanceStats.waitTimedoutNonzero;
        else if (dwWaitResult == WAIT_TIMEOUT && maxds == 0) ++performanceStats.waitTimedoutZero;
        else if (dwWaitResult == WAIT_IO_COMPLETION) ++performanceStats.waitIOCompleted;
        else if (dwWaitResult >= WAIT_OBJECT_0) ++performanceStats.waitSignalled;
#endif

        if ((dwWaitResult == WAIT_TIMEOUT) || (dwWaitResult == WAIT_IO_COMPLETION) || maxds == 0 || (dwWaitResult == WAIT_FAILED))
        {
            r |= NEEDEXEC;
        }
        if ((dwWaitResult >= WAIT_OBJECT_0) && (dwWaitResult < WAIT_OBJECT_0 + flags.size()))
        {
            r |= flags[dwWaitResult - WAIT_OBJECT_0];
        }
    }
    else
    {
        assert(false); // alert developers that we're hitting the limit
        r |= NEEDEXEC;
    }

    index = 0;

    return r;
}

// add handle to the list - must not be called twice with the same handle
// return true if handle added
bool WinWaiter::addhandle(HANDLE handle, int flag)
{
    assert(handles.size() == flags.size() && handles.size() >= index);

#ifdef DEBUG
    for (size_t i = index; i--; )
    {
        // double check we only add one of each handle
        assert(handles[i] != handle);
    }
#endif

    if (index < handles.size())
    {
        handles[index] = handle;
        flags[index] = flag;
    }
    else
    {
        handles.push_back(handle);
        flags.push_back(flag);
    }
    ++index;

    return true;
}

void WinWaiter::notify()
{
    SetEvent(externalEvent);
}
} // namespace
