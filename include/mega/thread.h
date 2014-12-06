/**
 * @file mega/thread.h
 * @brief Generic thread/mutex handling
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#ifndef MEGA_THREAD_H
#define MEGA_THREAD_H 1

namespace mega {
class Thread
{
public:
    virtual void start(void *(*start_routine)(void*), void *parameter) = 0;
    virtual void join() = 0;
};

class Mutex
{
public:
    virtual void init(bool recursive) = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

} // namespace

#endif

