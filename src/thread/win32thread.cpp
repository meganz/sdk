/**
 * @file thread/win32thread.cpp
 * @brief Win32 thread/mutex handling
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
 *
 * This file is also distributed under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
 */

#include "mega/thread/win32thread.h"
#include "mega/logging.h"
#include <limits.h>

#ifdef _WIN32
#include "windows.h"
#endif

namespace mega {
//Thread
Win32Thread::Win32Thread()
{

}

DWORD WINAPI Win32Thread::run(LPVOID lpParameter)
{
	Win32Thread *object = (Win32Thread *)lpParameter;
	return (DWORD)object->start_routine(object->pointer);
}

void Win32Thread::start(void *(*start_routine)(void*), void *parameter)
{
    this->start_routine = start_routine;
    this->pointer = parameter;

    hThread = CreateThread(NULL, 0, Win32Thread::run, this, 0, NULL);
}

void Win32Thread::join()
{
    WaitForSingleObject(hThread, INFINITE);
}

unsigned long long Win32Thread::currentThreadId()
{
    return (unsigned long long) GetCurrentThreadId();
}

Win32Thread::~Win32Thread()
{
    CloseHandle(hThread);
}

//Mutex
Win32Mutex::Win32Mutex()
{
    InitializeCriticalSection(&mutex);
}

Win32Mutex::Win32Mutex(bool recursive)
{
    InitializeCriticalSection(&mutex);

    init(recursive);        // just for correctness
}

void Win32Mutex::init(bool recursive)
{

}

void Win32Mutex::lock()
{
    EnterCriticalSection(&mutex);
}

void Win32Mutex::unlock()
{
    LeaveCriticalSection(&mutex);
}

Win32Mutex::~Win32Mutex()
{
    DeleteCriticalSection(&mutex);
}

//Semaphore
Win32Semaphore::Win32Semaphore()
{
    semaphore = CreateSemaphore(NULL, 0, INT_MAX, NULL);
    if (semaphore == NULL)
    {
        LOG_fatal << "Error creating semaphore: " << GetLastError();
    }
}

void Win32Semaphore::release()
{
    if (!ReleaseSemaphore(semaphore, 1, NULL))
    {
        LOG_fatal << "Error in ReleaseSemaphore: " << GetLastError();
    }
}

void Win32Semaphore::wait()
{
    DWORD ret = WaitForSingleObject(semaphore, INFINITE);
    if (ret == WAIT_OBJECT_0)
    {
        return;
    }

    LOG_fatal << "Error in WaitForSingleObject: " << GetLastError();
}

int Win32Semaphore::timedwait(int milliseconds)
{
    DWORD ret = WaitForSingleObject(semaphore, milliseconds);
    if (ret == WAIT_OBJECT_0)
    {
        return 0;
    }

    if (ret == WAIT_TIMEOUT)
    {
        return -1;
    }

    LOG_err << "Error in WaitForSingleObject: " << GetLastError();
    return -2;
}

Win32Semaphore::~Win32Semaphore()
{
    CloseHandle(semaphore);
}

} // namespace
