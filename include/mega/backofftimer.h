/**
 * @file mega/backofftimer.h
 * @brief Generic timer facility with exponential backoff
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

#ifndef MEGA_BACKOFF_TIMER_H
#define MEGA_BACKOFF_TIMER_H 1

#include "types.h"

namespace mega {
// generic timer facility with exponential backoff
class MEGA_API BackoffTimer
{
    dstime next;
    dstime delta;
    dstime base;
    PrnGen &rng;

public:
    // reset timer
    void reset();

    // trigger exponential backoff
    void backoff();

    // set absolute backoff
    void backoff(dstime);

    // set absolute trigger time
    void set(dstime);

    // check if timer has elapsed
    bool armed() const;

    // arm timer
    bool arm();

    // time left for event to become armed
    dstime retryin() const;

    // current backoff delta
    dstime backoffdelta();

    // time of next trigger or 0 if no trigger since last backoff
    dstime nextset() const;

    // update time to wait
    void update(dstime*);

    BackoffTimer(PrnGen &rng);
};

class MEGA_API BackoffTimerTracked;

// This class keeps track of a group of BackoffTimerTracked, which register and deregister themselves.
// Timers are in the multimap when they have non-0 non-NEVER timeouts set, giving us a much smaller group should we need to iterate it.
class MEGA_API BackoffTimerGroupTracker
{
    std::multimap<dstime, BackoffTimerTracked*> timeouts;

public:
    typedef std::multimap<dstime, BackoffTimerTracked*>::iterator Iter;

    inline Iter add(BackoffTimerTracked* bt);
    inline void remove(Iter i) { timeouts.erase(i); }

    // Find out the soonest (non-0 and non-NEVER) timeout in the group.
    // For transfers, it calls set(0) on any timed out timers, as the old code did.
    void update(dstime* waituntil, bool transfers);
};


// Just like a backoff timer, but is part of a group where we want to know the soonest (non-0) timeout in the group immediately
// Also, the enable() function can be used to exclude timers when they are not relevant, while keeping the timer settings.
class MEGA_API BackoffTimerTracked
{
    bool mIsEnabled;
    BackoffTimer bt;
    BackoffTimerGroupTracker& mTracker;
    BackoffTimerGroupTracker::Iter mTrackerPos;

    void untrack();
    void track();

public:
    BackoffTimerTracked(PrnGen &rng, BackoffTimerGroupTracker& tr);
    ~BackoffTimerTracked();

    inline bool arm()               { untrack(); bool result = bt.arm();   track(); return result; }
    inline void backoff()           { untrack(); bt.backoff();             track(); }
    inline void backoff(dstime t)   { untrack(); bt.backoff(t);            track(); }
    inline void set(dstime t)       { untrack(); bt.set(t);                track(); }
    inline void update(dstime* t)   { untrack(); bt.update(t);             track(); }
    inline void reset()             { untrack(); bt.reset();               track(); }

    inline bool armed() const       { return bt.armed(); };
    inline dstime nextset() const   { return bt.nextset(); };
    inline dstime retryin() const   { return bt.retryin(); }

    inline void enable(bool b)      { untrack(); mIsEnabled = b; track(); }
    inline bool enabled()           { return mIsEnabled; }
};

inline auto BackoffTimerGroupTracker::add(BackoffTimerTracked* bt) -> Iter
{
    return timeouts.emplace(bt->nextset() ? bt->nextset() : NEVER, bt);
}

inline void BackoffTimerTracked::untrack()
{
    if (mIsEnabled && bt.nextset() != 0 && bt.nextset() != NEVER)
    {
        mTracker.remove(mTrackerPos);
        mTrackerPos = BackoffTimerGroupTracker::Iter();
    }
}

inline void BackoffTimerTracked::track()
{
    if (mIsEnabled && bt.nextset() != 0 && bt.nextset() != NEVER)
    {
        mTrackerPos = mTracker.add(this);
    }
}

inline BackoffTimerTracked::BackoffTimerTracked(PrnGen &rng, BackoffTimerGroupTracker& tr) : mIsEnabled(true), bt(rng), mTracker(tr)
{
    track();
}

inline BackoffTimerTracked::~BackoffTimerTracked()
{
    untrack();
}


class MEGA_API TimerWithBackoff: public BackoffTimer {

public:
    int tag;
    TimerWithBackoff(PrnGen &rng, int tag);
};

} // namespace

#endif
