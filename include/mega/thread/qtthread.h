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

#ifdef USE_QT

#ifndef THREAD_CLASS
#define THREAD_CLASS QtThread
#define MUTEX_CLASS QtMutex
#define SEMAPHORE_CLASS QtSemaphore

#include "mega/thread.h"
#include <QThread>
#include <QMutex>
#include <QSemaphore>

namespace mega {
class QtThread : public QThread, public Thread
{
public:
    QtThread();
    virtual void start(void *(*start_routine)(void*), void *parameter);
    virtual void join();
    virtual ~QtThread();

    static unsigned long long currentThreadId();

protected:
    virtual void run();

    void *(*start_routine)(void*);
    void *pointer;
};

class QtMutex : public Mutex
{
public:
    QtMutex();
    QtMutex(bool recursive);
    virtual void init(bool recursive);
    virtual void lock();
    virtual void unlock();
    virtual ~QtMutex();

protected:
    QMutex *mutex;
};


class QtSemaphore : public Semaphore
{
public:
    QtSemaphore();
    virtual void wait();
    virtual int timedwait(int milliseconds);
    virtual void release();
    virtual ~QtSemaphore();

protected:
    QSemaphore *semaphore;
};

} // namespace

#endif

#endif
