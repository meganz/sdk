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
#include "mega/thread/cppthread.h"

namespace mega {

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

CppThread::~CppThread()
{
	delete thread;
}

	
CppMutex::CppMutex()
{
	mutex = NULL;
	rmutex = NULL;
}

void CppMutex::init(bool recursive = true)
{
	if (mutex || rmutex) return;

    if(recursive)
		rmutex = new std::recursive_mutex;
    else
		mutex = new std::mutex;
}

void CppMutex::lock()
{
	mutex ? mutex->lock() : rmutex->lock();
}

void CppMutex::unlock()
{
	mutex ? mutex->unlock() : rmutex->unlock();
}

CppMutex::~CppMutex()
{
	delete mutex;
	delete rmutex;
}

} // namespace
