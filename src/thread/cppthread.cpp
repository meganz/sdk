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

#include "mega/thread/cppthread.h"
#if defined USE_CPPTHREAD

#include <ctime>
#include <chrono>

#ifdef _WIN32
#pragma push_macro("NOMINMAX")
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#pragma pop_macro("NOMINMAX")
#endif

namespace mega {

//thread
CppThread::CppThread()
{
    thread = NULL;
}

void CppThread::start(void *(*start_routine)(void*), void *parameter)
{
    thread = new std::thread(start_routine, parameter);
}

void CppThread::join()
{
    thread->join();
}

bool CppThread::isCurrentThread() {
    return thread->get_id() == std::this_thread::get_id();
}

unsigned long long CppThread::currentThreadId()
{
#ifdef _WIN32
    return (unsigned long long) GetCurrentThreadId();
#else
    return (unsigned long long) &errno;
#endif
}

CppThread::~CppThread()
{
    delete thread;
}

//semaphore
CppSemaphore::CppSemaphore()
{
    count = 0;
}

void CppSemaphore::release()
{
    std::unique_lock<std::mutex> lock(mtx);
    count++;
    cv.notify_one();
}

void CppSemaphore::wait()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this]{return count > 0;});
    count--;
}

int CppSemaphore::timedwait(int milliseconds)
{
    std::chrono::system_clock::time_point endTime = std::chrono::system_clock::now()
		+ std::chrono::milliseconds(milliseconds);

    std::unique_lock<std::mutex> lock(mtx);
    while (!count)
    {
        auto status = cv.wait_until(lock, endTime);
        if (status == std::cv_status::timeout)
        {
            return -1;
        }
    }

    count--;
    return 0;
}

CppSemaphore::~CppSemaphore()
{
}

} // namespace

#endif
