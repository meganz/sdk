#pragma once

#include <mega/fuse/common/lockable_forward.h>
#include <mega/fuse/common/logging.h>

namespace mega
{
namespace fuse
{

template<typename Derived>
class Lockable
{
    // Convenience.
    using Traits   = LockableTraits<Derived>;
    using LockType = typename Traits::LockType;

    // Helpers.
    void doLock(void (LockType::*lock)()) const
    {
        auto& self = static_cast<const Derived&>(*this);

        Traits::acquiring(self);

        (mLock.*lock)();

        Traits::acquired(self);
    }

    bool doTryLock(bool (LockType::*tryLock)()) const
    {
        auto& self = static_cast<const Derived&>(*this);

        Traits::tryAcquire(self);

        if ((mLock.*tryLock)())
        {
            Traits::acquired(self);

            return true;
        }

        Traits::couldntAcquire(self);

        return false;
    }

    void doUnlock(void (LockType::*unlock)()) const
    {
        auto& self = static_cast<const Derived&>(*this);

        Traits::released(self);

        (mLock.*unlock)();
    }

    // Serializes access to this object.
    mutable LockType mLock;

protected:
    Lockable() = default;

    ~Lockable() = default;

public:
    // Acquire an exclusive lock on this object.
    void lock() const
    {
        doLock(&LockType::lock);
    }

    // Acquire an shared lock on this object.
    void lock_shared() const
    {
        doLock(&LockType::lock_shared);
    }

    // Try and acquire an exclusive lock on this object.
    bool try_lock() const
    {
        return doTryLock(&LockType::try_lock);
    }

    // Try and acquire a shared lock on this object.
    bool try_lock_shared() const
    {
        return doTryLock(&LockType::try_lock_shared);
    }

    // Release an exclusive lock on this object.
    void unlock() const
    {
        doUnlock(&LockType::unlock);
    }

    // Release a shared lock on this object.
    void unlock_shared() const
    {
        doUnlock(&LockType::unlock_shared);
    }
}; // Lockable

// For convenience.
template<typename T, typename LockType_>
struct LockableTraitsCommon
{
    // What kind of lock does T require?
    using LockType = LockType_;

    // Default loggers do nothing.
    static void acquiring(const T&)
    {
    }

    static void acquired(const T&)
    {
    }

    static void couldntAcquire(const T&)
    {
    }

    static void tryAcquire(const T&)
    {
    }

    static void released(const T&)
    {
    }
}; // LockableTraitsCommon<T, LockType_>

} // fuse
} // mega

