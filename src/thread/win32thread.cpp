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
 */

#include "mega.h"
#include "mega/thread/win32thread.h"


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

Win32Thread::~Win32Thread()
{
	CloseHandle(hThread);
}

//Mutex
Win32Mutex::Win32Mutex()
{
	InitializeCriticalSection(&mutex);
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
    semaphore=CreateSemaphore(
            NULL,           // default security attributes
            0,  // initial count
            INT_MAX,  // maximum count //TODO: may require <limits.h>
            NULL);          // unnamed semaphore
}
void Win32Mutex::release()
{
    ReleaseSemaphore(semaphore,1,NULL);
}
void Win32Mutex::wait()
{
    WaitForSingleObject(semaphore, INFINITE);
}
int Win32Mutex::timedwait(int milliseconds)
{
    DWORD ret = WaitForSingleObject(semaphore,milliseconds);
    if (ret == WAIT_OBJECT_0)
    {
        return 0;
    }
    else if (ret == WAIT_TIMEOUT) {
        return -1; //timed out
    }
    else
    {
        return -2; //undefined failure
    }
}
Win32Mutex::~Win32Semaphore()
{
    CloseHandle(semaphore);
}

} // namespace
