/**
 * @file mega/posix/megawaiter.h
 * @brief POSIX event/timeout handling
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
 *
 * This file is also distributed under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
 */

#if defined USE_CPPTHREAD

#ifndef THREAD_CLASS
#define THREAD_CLASS CppThread
#define SEMAPHORE_CLASS CppSemaphore

#include "mega/thread.h"

#include <thread>
#include <mutex>
#include <condition_variable>

namespace mega {

class CppThread : public Thread
{
public:
    CppThread();
    virtual void start(void *(*start_routine)(void*), void *parameter);
    virtual void join();
    virtual bool isCurrentThread();
    virtual ~CppThread();

    static unsigned long long currentThreadId();

protected:
    std::thread *thread;
};

	
class CppSemaphore : public Semaphore
{
public:
    CppSemaphore();
    virtual void release();
    virtual void wait();
    virtual int timedwait(int milliseconds);
    virtual ~CppSemaphore();

protected:
    std::mutex mtx;
    unsigned int count;
    std::condition_variable cv;
};

} // namespace

#endif

#endif
