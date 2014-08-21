/**
 * @file posix/wait.cpp
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
 */

#include "mega.h"
#include "mega/thread/posixthread.h"

namespace mega {

PosixThread::PosixThread()
{
    thread = new pthread_t;
}

void PosixThread::start(void *(*start_routine)(void*), void *parameter)
{
    pthread_create(thread, NULL, start_routine, parameter);
}

void PosixThread::join()
{
    pthread_join(*thread, NULL);
}

PosixThread::~PosixThread()
{
    delete thread;
}


PosixMutex::PosixMutex()
{
    mutex = NULL;
    attr = NULL;
}

void PosixMutex::init(bool recursive = true)
{
    if(recursive)
    {
        mutex = new pthread_mutex_t;
        attr = new pthread_mutexattr_t;
        pthread_mutexattr_init(attr);
        pthread_mutexattr_settype(attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(mutex, attr);
    }
    else
    {
        mutex = new pthread_mutex_t;
        pthread_mutex_init(mutex, NULL);
    }
}

void PosixMutex::lock()
{
    pthread_mutex_lock(mutex);
}

void PosixMutex::unlock()
{
    pthread_mutex_unlock(mutex);
}

PosixMutex::~PosixMutex()
{
    delete mutex;
    pthread_mutexattr_destroy(attr);
    delete attr;
}


} // namespace
