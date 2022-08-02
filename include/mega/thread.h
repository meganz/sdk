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
 *
 * This file is also distributed under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
 */

#ifndef MEGA_THREAD_H
#define MEGA_THREAD_H 1

namespace mega {
class Thread
{
public:
    virtual void start(void *(*start_routine)(void*), void *parameter) = 0;
    virtual void join() = 0;
    virtual bool isCurrentThread() = 0;
};

class Semaphore
{
public:
    virtual void release() = 0;
    virtual void wait() = 0;
    virtual int timedwait(int milliseconds) = 0;
};

} // namespace

#endif

