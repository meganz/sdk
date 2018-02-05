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
 *
 * This file is also distributed under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
 */

#include "mega/thread/posixthread.h"
#include "mega/logging.h"
#include <sys/time.h>
#include <errno.h>

#ifdef USE_PTHREAD
#if defined (__MINGW32__) && !defined(__struct_timespec_defined)
struct timespec
{
  long long	tv_sec; 	/* seconds */
  long  	tv_nsec;	/* nanoseconds */
};
# define __struct_timespec_defined  1
#endif

namespace mega {

PosixThread::PosixThread()
{
    thread = new pthread_t();
}

void PosixThread::start(void *(*start_routine)(void*), void *parameter)
{
    pthread_create(thread, NULL, start_routine, parameter);
}

void PosixThread::join()
{
    pthread_join(*thread, NULL);
}

unsigned long long PosixThread::currentThreadId()
{
#if defined(_WIN32) && !defined(__WINPTHREADS_VERSION)
    return (unsigned long long) pthread_self().x;
#else
    return (unsigned long long) pthread_self();
#endif
}

PosixThread::~PosixThread()
{
    delete thread;
}

//PosixMutex
PosixMutex::PosixMutex()
{
    mutex = NULL;
    attr = NULL;
}

PosixMutex::PosixMutex(bool recursive)
{
    mutex = NULL;
    attr = NULL;

    init(recursive);
}

void PosixMutex::init(bool recursive)
{
    if (recursive)
    {
        mutex = new pthread_mutex_t();
        attr = new pthread_mutexattr_t();
        pthread_mutexattr_init(attr);
        pthread_mutexattr_settype(attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(mutex, attr);
    }
    else
    {
        mutex = new pthread_mutex_t();
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
    if (mutex)
    {
        pthread_mutex_destroy(mutex);
        delete mutex;
    }

    if (attr)
    {
        pthread_mutexattr_destroy(attr);
        delete attr;
    }
}

//PosixSemaphore
PosixSemaphore::PosixSemaphore()
{
    count = 0;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(&mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&cv, NULL);
}

void PosixSemaphore::wait()
{
    pthread_mutex_lock(&mtx);
    while (!count)
    {
        int ret = pthread_cond_wait(&cv,&mtx);
        if (ret)
        {
            pthread_mutex_unlock(&mtx);
            LOG_fatal << "Error in sem_wait: " << ret;
            return;
        }
    }
    count--;
    pthread_mutex_unlock(&mtx);
}

static inline
void timespec_add_msec(struct timespec *tv, int milliseconds)
{
    int seconds = milliseconds / 1000;
    int milliseconds_left = milliseconds % 1000;

    tv->tv_sec += seconds;
    tv->tv_nsec += milliseconds_left * 1000000;

    if (tv->tv_nsec >= 1000000000)
    {
        tv->tv_nsec -= 1000000000;
        tv->tv_sec++;
    }
}

int PosixSemaphore::timedwait(int milliseconds)
{
    struct timespec ts;
    struct timeval now;

    int ret = gettimeofday(&now, NULL); //not Y2K38 safe :-D
    if (ret)
    {
        LOG_err << "Error in gettimeofday: " << ret;
        return -2;
    }
    ts.tv_sec = now.tv_sec;
    ts.tv_nsec = now.tv_usec * 1000;
    timespec_add_msec (&ts, milliseconds);

    pthread_mutex_lock(&mtx);
    while (!count)
    {
        int ret = pthread_cond_timedwait(&cv, &mtx, &ts);
        if (ret == ETIMEDOUT)
        {
            pthread_mutex_unlock(&mtx);
            return -1;
        }

        if (ret)
        {
            pthread_mutex_unlock(&mtx);
            LOG_err << "Unexpected error in pthread_cond_timedwait: " << ret;
            return -2;
        }
    }

    count--;
    pthread_mutex_unlock(&mtx);
    return 0;
}

void PosixSemaphore::release()
{
    pthread_mutex_lock(&mtx);
    count++;
    int ret = pthread_cond_signal(&cv);
    if (ret)
    {
        LOG_fatal << "Unexpected error in pthread_cond_signal: " << ret;
    }
    pthread_mutex_unlock(&mtx);
}

PosixSemaphore::~PosixSemaphore()
{
    pthread_mutex_destroy(&mtx);
    pthread_cond_destroy(&cv);
}

}// namespace

#endif
