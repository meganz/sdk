#pragma once

#include <cassert>
#include <mutex>

#include <mega/fuse/common/any_lock_set_forward.h>
#include <mega/fuse/common/any_lock_forward.h>

namespace mega
{
namespace fuse
{

// Allows the user to treat multiple locks as if they were one.
class AnyLockSet
{
    // What locks does this set contain?
    AnyLockVector mLocks;

    // Do we own all the locks in the set?
    bool mOwned;

public:
    AnyLockSet();

    AnyLockSet(AnyLockSet&& other);

    AnyLockSet& operator=(AnyLockSet&& rhs);

    // Do we own all the locks in this set?
    operator bool() const;

    // Clear all locks from the set.
    void clear();

    // Does this set contain any locks?
    bool empty() const;

    // Add a lock to the set.
    template<typename T>
    void emplace(T& lock)
    {
        // You should never add a lock when locks are held.
        assert(!mOwned);

        // Add the lock to the set.
        mLocks.emplace_back(lock, std::defer_lock);
    }

    // Add a number of locks to the set.
    template<typename T, typename... Ts>
    void emplace(T& first, Ts&... rest)
    {
        emplace(first);
        emplace(rest...);
    }

    // Acquire each lock in the set.
    //
    // Control will not return to the caller until each lock in the set
    // has been acquired. If the method can't acquire a given lock, it
    // releases any locks it did acquire and retries. That is, locks are
    // acquired in a dead-lock free manner.
    void lock();

    // Do we own all the locks in this set?
    bool owns_lock() const;

    // Release ownership of each lock in the set.
    void release();

    // How many locks does this set contain?
    AnyLockVector::size_type size() const;

    // Swap this set with another.
    void swap(AnyLockSet& other);

    // Try and acquire each lock in the set.
    //
    // Control returns immediately to the caller in all cases.
    //
    // If the method is unable to acquire a lock in the set, it releases any
    // locks it was able to acquire and returns false to the caller.
    bool try_lock();

    // Unlock each lock in the set.
    void unlock();
}; // AnyLockSet

// Creates an unlocked set containing the specified locks.
//
// The behavior of this function is modeled after std::unique_lock:
//
// std::mutex ma;
// std::mutex mb;
//
// std::unique_lock<std::mutex> l0(ma, std::defer);
// std::unique_lock<std::mutex> l1(mb, std::defer);
//
// std::lock(l0, l1);
//
// Put differently: We want to group together a bunch of locks so
// to make life a little more convenient but we may not want to
// actually acquire those locks immediately. Maybe we want to pass them
// somewhere else and acquire them later, or perhaps we want to acquire
// them in combination with other locks via std::lock(...).
template<typename T, typename... Ts>
AnyLockSet deferredLockAll(T& first, Ts&... rest)
{
    AnyLockSet locks;

    // Add locks to the set.
    locks.emplace(first, rest...);

    // Return set to caller.
    return locks;
}

// Creates a locked set containing the specified locks.
template<typename T, typename... Ts>
AnyLockSet lockAll(T& first, Ts&... rest)
{
    // Add locks to a new set.
    auto locks = deferredLockAll(first, rest...);

    // Acquire locks.
    locks.lock();

    // Return set to caller.
    return locks;
}

} // fuse
} // mega

