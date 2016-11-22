/**
 * @file thread/libuvthread.cpp
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

#include "mega.h"
#include "mega/thread/libuvthread.h"

#ifdef HAVE_LIBUV

namespace mega {

//LibUVThread
LibUVThread::LibUVThread()
{
    thread = new uv_thread_t;
}

void LibUVThread::run(void *arg)
{
    LibUVThread *object = (LibUVThread *)arg;
    object->start_routine(object->pointer);
}

void LibUVThread::start(void *(*start_routine)(void*), void *parameter)
{
    this->start_routine = start_routine;
    this->pointer = parameter;

    uv_thread_create(thread, LibUVThread::run, this);
}

void LibUVThread::join()
{
    uv_thread_join(thread);
}

uint64_t LibUVThread::currentThreadId()
{
    return (uint64_t) uv_thread_self();
}

LibUVThread::~LibUVThread()
{
    delete thread;
}


//LibUVMutex
LibUVMutex::LibUVMutex()
{
    mutex = NULL;
    count = NULL;
}

LibUVMutex::LibUVMutex(bool recursive)
{
    mutex = NULL;
    count = NULL;

    init(recursive);
}

void LibUVMutex::init(bool recursive)
{
    mutex = new uv_mutex_t;
    uv_mutex_init(mutex);

    if (recursive)
    {
        count = new uv_key_t;
        uv_key_create(count);
    }
}

void LibUVMutex::lock()
{
    int value;
    if (count)
    {
        value = (int)uv_key_get(count);
        if (value)
        {
            uv_key_set(count, (void *)(value + 1));
            return;
        }
    }
    uv_mutex_lock(mutex);
    if (count)
    {
        uv_key_set(count, (void *)(value + 1));
    }
}

void LibUVMutex::unlock()
{
    int value;
    if (count)
    {
        value = (int)uv_key_get(count);
        if (value > 1)
        {
            uv_key_set(count, (void *)(value - 1));
            return;
        }
    }
    uv_mutex_unlock(mutex);
    if (count)
    {
        uv_key_set(count, (void *)(value - 1));
    }
}

LibUVMutex::~LibUVMutex()
{
    uv_mutex_destroy(mutex);
    delete mutex;
    if (count)
    {
        uv_key_delete(count);
        delete count;
    }
}

//LibUVSemaphore
LibUVSemaphore::LibUVSemaphore()
{
    semaphore = new uv_sem_t;
    if (uv_sem_init(semaphore, 0) != 0)
    {
        LOG_fatal << "Error creating semaphore";
    }
}

void LibUVSemaphore::wait()
{
    uv_sem_wait(semaphore);
}

int LibUVSemaphore::timedwait(int milliseconds)
{
    while (uv_sem_trywait(semaphore) != 0)
    {
        if (milliseconds < 0)
        {
            return -1;
        }

#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000)
#endif

        milliseconds -= 100;
    }
    return 0;
}

void LibUVSemaphore::release()
{
    uv_sem_post(semaphore);
}

LibUVSemaphore::~LibUVSemaphore()
{
    uv_sem_destroy(semaphore);
    delete semaphore;
}

}// namespace

#endif
