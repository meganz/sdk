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
 */

#ifdef USE_PTHREAD

#ifndef THREAD_CLASS
#define THREAD_CLASS PosixThread

#include "mega/thread.h"
#include <pthread.h>

namespace mega {
class PosixThread : public Thread
{
public:
    PosixThread();
    void start(void *(*start_routine)(void*), void *parameter);
    void join();
    virtual ~PosixThread();

protected:
    pthread_t *thread;
};

class PosixMutex : public Mutex
{
public:
    PosixMutex();
    virtual void init(bool recursive);
    virtual void lock();
    virtual void unlock();
    virtual ~PosixMutex();

protected:
    pthread_mutex_t *mutex;
    pthread_mutexattr_t *attr;
};

} // namespace

#endif

#endif
