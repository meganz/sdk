#include <cassert>
#include <functional>
#include <utility>

#include <mega/fuse/common/any_lock_set.h>
#include <mega/fuse/common/any_lock.h>

namespace mega
{
namespace fuse
{

struct Locker
{
    // Try and acquire the specified lock.
    std::function<bool(AnyLock&)> lock;

    // Should we abort locking if we can't acquire a single lock?
    bool shouldAbort;
}; /* Locker */

static const Locker blocking = {
    [](AnyLock& lock) {
        return lock.lock(), true;
    },
    false
}; /* blocking */

static const Locker nonblocking = {
    [](AnyLock& lock) {
        return lock.try_lock();
    },
    true
}; /* nonblocking */

// Tries to acquire all locks in a set.
static bool tryLockAll(AnyLockVector& locks, const Locker& locker);

AnyLockSet::AnyLockSet()
  : mLocks()
  , mOwned(false)
{
}

AnyLockSet::AnyLockSet(AnyLockSet&& other)
  : mLocks(std::move(other.mLocks))
  , mOwned(std::move(other.mOwned))
{
    other.mOwned = false;
}

AnyLockSet& AnyLockSet::operator=(AnyLockSet&& rhs)
{
    AnyLockSet temp(std::move(rhs));

    swap(temp);

    return *this;
}

AnyLockSet::operator bool() const
{
    return owns_lock();
}

void AnyLockSet::clear()
{
    mLocks.clear();
}

bool AnyLockSet::empty() const
{
    return mLocks.empty();
}

void AnyLockSet::lock()
{
    assert(!mLocks.empty());
    assert(!mOwned);

    mOwned = tryLockAll(mLocks, blocking);
}

bool AnyLockSet::owns_lock() const
{
    return mOwned;
}

void AnyLockSet::release()
{
    if (mOwned)
    {
        while (!mLocks.empty())
        {
            mLocks.back().release();
            mLocks.pop_back();
        }
    }

    mOwned = false;
}

AnyLockVector::size_type AnyLockSet::size() const
{
    return mLocks.size();
}

void AnyLockSet::swap(AnyLockSet& other)
{
    using std::swap;

    swap(mLocks, other.mLocks);
    swap(mOwned, other.mOwned);
}

bool AnyLockSet::try_lock()
{
    assert(!mLocks.empty());
    assert(!mOwned);

    mOwned = tryLockAll(mLocks, nonblocking);
    
    return mOwned;
}

void AnyLockSet::unlock()
{
    assert(mOwned);

    for (auto& lock : mLocks)
        lock.unlock();

    mOwned = false;
}

bool lock(AnyLock& lock)
{
    return lock.lock(), true;
}

bool tryLock(AnyLock& lock)
{
    return lock.try_lock();
}

// This function's responsible for acquiring a vector of locks in a
// dead-lock safe manner.
//
// The way it works is pretty straight forward: Try to acquire each lock in
// turn, remembering which locks were successfully acquired. If any lock
// couldn't be acquired, release the locks that we have acquired and repeat
// the process starting from the lock we couldn't acquire.
//
// The first lock is acquired is a special way, depending on how this
// function was called. If the user wants to block until all locks have been
// acquired, the call to acquire the first lock is itself blocking. Each
// subsequent lock is acquired in a nonblocking manner so that we can
// release any held locks if we're unable to acquire a particular lock. 
//
// If the user doesn't want to block and we couldn't acquire the first lock,
// we just return false to the caller immediately.
bool tryLockAll(AnyLockVector& locks, const Locker& locker)
{
    using Index = AnyLockVector::size_type;

    // Sanity.
    assert(!locks.empty());
    assert(locker.lock);

    // What lock should we acquire first?
    Index first = 0;

    // How many locks have we acquired?
    Index count = 0;

    // How many locks do we have to acquire?
    Index num = locks.size();

    try
    {
        do
        {
            // Try and acquire the first lock.
            if (!locker.lock(locks[first]))
                return false;

            // Try and acquire the rest of the locks.
            for (count = 1; count < num; ++count)
            {
                // Calculate index of next lock.
                auto index = (first + count) % num;

                // Try and acquire the next lock.
                if (locks[index].try_lock())
                    continue;

                // Release acquired locks.
                while (count--)
                    locks[(first + count) % num].unlock();

                // Should we fail immediately?
                if (locker.shouldAbort)
                    return false;

                // Start the next iteration by acquiring the failing lock.
                first = index;

                // Try and acquire the locks again.
                break;
            }
        }
        while (!locks[first].owns_lock());
    }
    catch (...)
    {
        // Release acquired locks.
        while (count--)
            locks[(first + count) & num].unlock();

        // Rethrow exception.
        throw;
    }

    // We've acquired all the locks in the set.
    return true;
}

} // fuse
} // mega

