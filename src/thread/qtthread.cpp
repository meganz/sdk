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

#include "mega/thread/qtthread.h"

#define STACK_SIZE 1048576

namespace mega {

QtThread::QtThread()
{

}

void QtThread::run()
{
    start_routine(pointer);
}

void QtThread::start(void *(*start_routine)(void*), void *parameter)
{
    setStackSize(STACK_SIZE);
    this->start_routine = start_routine;
    this->pointer = parameter;

    QThread::start();
}

void QtThread::join()
{
    this->wait();
}

QtThread::~QtThread()
{

}

unsigned long long QtThread::currentThreadId()
{
    return (unsigned long long) QThread::currentThreadId();
}

//mutex
QtMutex::QtMutex()
{
    mutex = NULL;
}

QtMutex::QtMutex(bool recursive)
{
    mutex = NULL;

    init(recursive);
}


void QtMutex::init(bool recursive)
{
    if(recursive)
        mutex = new QMutex(QMutex::Recursive);
    else
        mutex = new QMutex();
}

void QtMutex::lock()
{
    mutex->lock();
}

void QtMutex::unlock()
{
    mutex->unlock();
}

QtMutex::~QtMutex()
{
    delete mutex;
}


//semaphore
QtSemaphore::QtSemaphore()
{
    semaphore = new QSemaphore();
}

void QtSemaphore::wait()
{
    semaphore->acquire();
}

int QtSemaphore::timedwait(int milliseconds)
{
    return semaphore->tryAcquire(1, milliseconds) ? 0 : -1;
}

void QtSemaphore::release()
{
    semaphore->release();
}

QtSemaphore::~QtSemaphore()
{
    delete semaphore;
}

} // namespace
