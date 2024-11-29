/**
 * @file mega/waiter.h
 * @brief Generic waiter interface
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

#ifndef MEGA_WAITER_H
#define MEGA_WAITER_H 1

#include <atomic>

#include "types.h"

namespace mega {

// interface enabling a class to add its wakeup criteria to the waiter
struct MEGA_API EventTrigger
{
    // add wakeup criterion
    virtual void addevents(Waiter*, int) = 0;

    // process events after wakeup
    virtual int checkevents(Waiter*)
    {
        return 0;
    }
    virtual ~EventTrigger() = default;
};

// wait for events
struct MEGA_API Waiter
{
    // current time (processwide)
    static std::atomic<dstime> ds;

    // set ds to current time
    static void bumpds();

    // This mutex protects concurrent updates of the value stored by the atomic "ds" 
    // when multiple threads are in bumpds()
    static std::mutex dsMutex;

    // wait ceiling
    std::atomic<dstime> maxds;

    // begin waiting cycle with timeout
    virtual void init(dstime);

    // add wakeup events
    void wakeupby(EventTrigger*, int);

    // wait for all added wakeup criteria (plus the host app's own), up to the
    // specified number of deciseconds
    virtual int wait() = 0;

    // force a wakeup
    virtual void notify() = 0;

    static const int NEEDEXEC = 1;
    static const int HAVESTDIN = 2;

    virtual ~Waiter() { }
};
} // namespace

#endif
