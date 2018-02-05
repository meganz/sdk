/**
 * @file mega/thread/win32thread.h
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
 * This file is also distributed with under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
 */

#if (defined(_WIN32) && !defined(WINDOWS_PHONE) || defined(USE_WIN32THREAD) )

#ifndef THREAD_CLASS
#define THREAD_CLASS Win32Thread
#define MUTEX_CLASS Win32Mutex
#define SEMAPHORE_CLASS Win32Semaphore

#include "mega/thread.h"
#include <windows.h>

namespace mega {
class Win32Thread : public Thread
{
public:
    Win32Thread();
    virtual void start(void *(*start_routine)(void*), void *parameter);
    virtual void join();
    virtual ~Win32Thread();

    void *(*start_routine)(void*);
    void *pointer;

    static unsigned long long currentThreadId();

protected:
    static DWORD WINAPI run(LPVOID lpParameter);
    HANDLE hThread;
};

class Win32Mutex : public Mutex
{
public:
    Win32Mutex();
    Win32Mutex(bool recursive);
    virtual void init(bool recursive);
    virtual void lock();
    virtual void unlock();
    virtual ~Win32Mutex();

protected:
    CRITICAL_SECTION mutex;
};

class Win32Semaphore : public Semaphore
{
public:
    Win32Semaphore();
    virtual void release();
    virtual void wait();
    virtual int timedwait(int milliseconds);
    virtual ~Win32Semaphore();

protected:
    HANDLE semaphore;
};

} // namespace

#endif

#endif
