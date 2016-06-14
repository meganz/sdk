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

#ifdef USE_PTHREAD

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

//PosixMutex
PosixMutex::PosixMutex()
{
    mutex = NULL;
    attr = NULL;
}

void PosixMutex::init(bool recursive)
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
    semaphore = new sem_t;
    if (sem_init(semaphore, 0, 0) == -1)
    {
        LOG_fatal << "Error creating semaphore: " << errno;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(&mtx,&attr);
    pthread_mutexattr_destroy(&attr);
}

void PosixSemaphore::wait()
{
    while (sem_wait(semaphore) == -1)
    {
        if (errno == EINTR)
        {
            continue;
        }

        LOG_fatal << "Error in sem_wait: " << errno;
    }
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


static inline
void timeval_add_msec(struct timeval *tv, int milliseconds)
{
    int seconds = milliseconds / 1000;
    int milliseconds_left = milliseconds % 1000;

    tv->tv_sec += seconds;
    tv->tv_usec += milliseconds_left * 1000;

    if (tv->tv_usec >= 1000000)
    {
        tv->tv_usec -= 1000000;
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
    while (true)
    {
        int ret = sem_trywait(semaphore);
        if (!ret)
        {
            pthread_mutex_unlock(&mtx);
            return 0;
        }
        else if (errno == EAGAIN)
        {
            //continue;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            LOG_err << "Error in sem_timedwait: " << ret;
            pthread_mutex_unlock(&mtx);
            return -2;
        }

        int retcontimeout = pthread_cond_timedwait(&cv,&mtx,&ts);
        if (retcontimeout == ETIMEDOUT)
        {
            pthread_mutex_unlock(&mtx);
            return -1;
        }
        else if (retcontimeout)
        {
            LOG_fatal << "Unexpected error in pthread_cond_timedwait: " << errno;
            pthread_mutex_unlock(&mtx);
            return -2;
        }
    }
    pthread_mutex_unlock(&mtx);
}

void PosixSemaphore::release()
{
    if (sem_post(semaphore) == -1)
    {
        LOG_fatal << "Error in sem_post: " << errno;
    }

    pthread_cond_signal(&cv);
}

PosixSemaphore::~PosixSemaphore()
{
    sem_destroy(semaphore);
    delete semaphore;
    pthread_mutex_destroy(&mtx);
}

}// namespace

#endif
