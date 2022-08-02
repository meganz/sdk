/**
 * @file mega/thread/libuvthread.h
 * @brief Implementation of thread functions based on libuv
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

#ifdef HAVE_LIBUV

#ifndef THREAD_CLASS
#define THREAD_CLASS LibUVThread
#define SEMAPHORE_CLASS LibUVSemaphore

#include "mega/thread.h"
#include <uv.h>

namespace mega {
class LibUVThread : public Thread
{
public:
    LibUVThread();
    void start(void *(*start_routine)(void*), void *parameter);
    void join();
    virtual bool isCurrentThread();
    virtual ~LibUVThread();

    void *(*start_routine)(void*);
    void *pointer;

    static uint64_t currentThreadId();

protected:
    uv_thread_t *thread;
    static void run(void *arg);
};

class LibUVSemaphore : public Semaphore
{
public:
    LibUVSemaphore();
    virtual void release();
    virtual void wait();
    virtual int timedwait(int milliseconds);
    virtual ~LibUVSemaphore();

protected:
    uv_sem_t* semaphore;
};

} // namespace

#endif

#endif
