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

#if defined(HAVE_CONFIG_H) || !(defined _WIN32)
// platform dependent constants
#ifdef __ANDROID__
#include "mega/config-android.h"
#else
#ifndef MEGA_GENERATED_CONFIG_H
#include "mega/config.h"
#define MEGA_GENERATED_CONFIG_H
#endif
#endif
#endif

#ifdef USE_PTHREAD

#ifndef THREAD_CLASS
#define THREAD_CLASS PosixThread
#define SEMAPHORE_CLASS PosixSemaphore

#include "mega/thread.h"
#include <pthread.h>
#include <semaphore.h>

namespace mega {
class PosixThread : public Thread
{
public:
    PosixThread();
    void start(void *(*start_routine)(void*), void *parameter);
    void join();
    bool isCurrentThread();
    virtual ~PosixThread();

    static unsigned long long currentThreadId();

protected:
    pthread_t *thread;
};

class PosixSemaphore : public Semaphore
{
public:
    PosixSemaphore();
    virtual void release();
    virtual void wait();
    virtual int timedwait(int milliseconds);
    virtual ~PosixSemaphore();

protected:
    unsigned int count;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
};

} // namespace

#endif

#endif
