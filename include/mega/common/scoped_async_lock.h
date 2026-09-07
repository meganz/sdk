#pragma once

#include <mega/common/assertions.h>
#include <mega/common/async_mutex_forward.h>
#include <mega/common/scoped_async_lock_forward.h>
#include <mega/common/utility.h>

#include <cassert>
#include <future>
#include <mutex>
#include <utility>

namespace mega
{
namespace common
{
namespace detail
{

// Represents a lock held on an async mutex.
template<template<typename> typename Derived, typename Mutex>
class ScopedAsyncLock
{
    // What mutex does this lock potentially own?
    Mutex* mMutex;

    // Does this lock currently own its mutex?
    bool mOwnsMutex;

protected:
    ScopedAsyncLock():
        mMutex(nullptr),
        mOwnsMutex(false)
    {}

    ScopedAsyncLock(Mutex& mutex):
        mMutex(&mutex),
        mOwnsMutex(false)
    {
        lock();
    }

    ScopedAsyncLock(Mutex& mutex, std::adopt_lock_t):
        mMutex(&mutex),
        mOwnsMutex(true)
    {}

    ScopedAsyncLock(Mutex& mutex, std::defer_lock_t):
        mMutex(&mutex),
        mOwnsMutex(false)
    {}

    ScopedAsyncLock(ScopedAsyncLock&& other):
        mMutex(std::exchange(other.mMutex, nullptr)),
        mOwnsMutex(std::exchange(other.mOwnsMutex, false))
    {}

    ~ScopedAsyncLock()
    {
        // No lock needs to be relinquished.
        if (!mOwnsMutex)
            return;

        // Make sure we reference a mutex.
        ASSERT_ALWAYS(mMutex);

        // Relinquish ownership of our mutex.
        Derived<Mutex>::doUnlock(*mMutex);
    }

    ScopedAsyncLock& operator=(ScopedAsyncLock&& rhs)
    {
        ScopedAsyncLock temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    // Abandon responsibility for any lock held on our mutex.
    void release()
    {
        mMutex = nullptr;
        mOwnsMutex = false;
    }

    // Transfer mutex ownership from one lock to another.
    void swap(ScopedAsyncLock& other)
    {
        // For ADL.
        using std::swap;

        swap(mMutex, other.mMutex);
        swap(mOwnsMutex, other.mOwnsMutex);
    }

public:
    // True if this lock currently owns its mutex.
    explicit operator bool() const
    {
        return owns_lock();
    }

    // True if this lock currently owns no mutex.
    bool operator!() const
    {
        return !owns_lock();
    }

    // Acquire ownership of this lock.
    void lock()
    {
        // Make sure we actually reference a mutex.
        ASSERT_ALWAYS(mMutex);

        // Make sure the lock isn't already held.
        ASSERT_ALWAYS(!mOwnsMutex);

        // Ask our subclass to acquire the lock.
        operator=(Derived<Mutex>::doLock(*mMutex));
    }

    // The mutex this lock may be managing.
    Mutex* mutex() const
    {
        return mMutex;
    }

    // True if this lock currently owns its mutex.
    bool owns_lock() const
    {
        return mOwnsMutex;
    }

    // Try and acquire ownership of this lock's mutex.
    bool try_lock()
    {
        // Make sure we actually reference a mutex.
        ASSERT_ALWAYS(mMutex);

        // Make sure we don't already own this mutex.
        ASSERT_ALWAYS(!mOwnsMutex);

        // Ask our subclass to acquire a lock on mutex.
        operator=(Derived<Mutex>::doTryLock(*mMutex));

        // Let our caller know if we now own this mutex.
        return mOwnsMutex;
    }

    // Relinquish ownership of this lock's mutex.
    void unlock()
    {
        // Make sure we actually reference a mutex.
        ASSERT_ALWAYS(mMutex);

        // And that we own that mutex.
        ASSERT_ALWAYS(mOwnsMutex);

        // Ask our subclass to relinquish ownership.
        Derived<Mutex>::doUnlock(*mMutex);

        // We no longer own a lock on our mutex.
        mOwnsMutex = false;
    }
}; // ScopedAsyncLock<Derived, Mutex>

// Represents a shared lock held on an async mutex.
template<typename Mutex>
class ScopedAsyncSharedLock final: public ScopedAsyncLock<ScopedAsyncSharedLock, Mutex>
{
    // Sanity.
    static_assert(IsAsyncMutexV<Mutex>);

    // Convenience.
    using Base = ScopedAsyncLock<ScopedAsyncSharedLock, Mutex>;

    // So our base class can invoke unlock(Mutex&).
    friend Base;

    // So mutex can invoke ScopedAsyncSharedLock(Mutex&).
    friend Mutex;

    // Called to acquire shared ownership of mutex.
    static auto doLock(Mutex& mutex)
    {
        return mutex.lockShared().get();
    }

    // Called to try and acquire shared ownership of mutex.
    static auto doTryLock(Mutex& mutex)
    {
        return mutex.tryLockShared();
    }

    // Called to relinquish ownership of mutex.
    static void doUnlock(Mutex& mutex)
    {
        mutex.unlockShared();
    }

public:
    ScopedAsyncSharedLock():
        Base()
    {}

    ScopedAsyncSharedLock(Mutex& mutex):
        Base(mutex)
    {}

    ScopedAsyncSharedLock(Mutex& mutex, std::adopt_lock_t tag):
        Base(mutex, tag)
    {}

    ScopedAsyncSharedLock(Mutex& mutex, std::defer_lock_t tag):
        Base(mutex, tag)
    {}

    ScopedAsyncSharedLock(ScopedAsyncSharedLock&& other):
        Base(std::move(other))
    {}

    ScopedAsyncSharedLock& operator=(ScopedAsyncSharedLock&& rhs)
    {
        Base::operator=(std::move(rhs));

        return *this;
    }

    // Promote this shared lock into a unique lock.
    template<typename Callback, std::enable_if_t<IsUniqueLockCallbackV<Callback, Mutex>>* = nullptr>
    void toUniqueLock(Callback&& callback)
    {
        auto* mutex = this->mutex();

        // Sanity: Make sure we reference a mutex.
        ASSERT_ALWAYS(mutex);

        // Sanity: Make sure we own a lock on mutex.
        ASSERT_ALWAYS(this->owns_lock());

        // This instance is no longer responsible for any lock on mutex.
        this->release();

        // Ask the mutex to promote our shared lock into a unique lock.
        mutex->toUniqueLock(std::forward<Callback>(callback));
    }

    // As above but more convenient for synchronous use.
    auto toUniqueLock()
    {
        // So we can transmit the lock to our waiter.
        std::promise<ScopedAsyncUniqueLock<Mutex>> notifier;

        // So we can wait until the lock has been promoted.
        auto waiter = notifier.get_future();

        // Promote our shared lock into a unique lock.
        toUniqueLock(
            [notifier = std::move(notifier)](auto&& lock) mutable
            {
                // Transmit lock to our waiter.
                notifier.set_value(std::forward<decltype(lock)>(lock));
            });

        // Return waiter to our caller.
        return waiter;
    }

    // Try and promote this shared lock into a unique lock.
    auto tryToUniqueLock()
    {
        auto* mutex = this->mutex();

        // Sanity.
        ASSERT_ALWAYS(mutex);

        // Try and promote this lock into a unique lock.
        auto lock = mutex->tryToUniqueLock();

        // Shared lock has been promoted into a unique lock.
        if (lock)
            this->release();

        // Return (possible) unique lock to our caller.
        return lock;
    }

    void swap(ScopedAsyncSharedLock& other)
    {
        Base::swap(other);
    }
}; // ScopedAsyncSharedLock<Mutex>

// Represents a unique lock held on an async mutex.
template<typename Mutex>
class ScopedAsyncUniqueLock final: public ScopedAsyncLock<ScopedAsyncUniqueLock, Mutex>
{
    // Sanity.
    static_assert(IsAsyncMutexV<Mutex>);

    // Convenience.
    using Base = ScopedAsyncLock<ScopedAsyncUniqueLock, Mutex>;

    // So our base class can invoke unlock(Mutex&).
    friend Base;

    // So mutex can invoke ScopedAsyncUniqueLock(Mutex&).
    friend Mutex;

    // Called to acquire exclusive ownership of mutex.
    static auto doLock(Mutex& mutex)
    {
        return mutex.lock().get();
    }

    // Called to try and acquire exclusive ownership of mutex.
    static auto doTryLock(Mutex& mutex)
    {
        return mutex.tryLock();
    }

    // Called to relinquish ownership of mutex.
    static void doUnlock(Mutex& mutex)
    {
        mutex.unlock();
    }

public:
    ScopedAsyncUniqueLock():
        Base()
    {}

    ScopedAsyncUniqueLock(Mutex& mutex):
        Base(mutex)
    {}

    ScopedAsyncUniqueLock(Mutex& mutex, std::adopt_lock_t tag):
        Base(mutex, tag)
    {}

    ScopedAsyncUniqueLock(Mutex& mutex, std::defer_lock_t tag):
        Base(mutex, tag)
    {}

    ScopedAsyncUniqueLock(ScopedAsyncUniqueLock&& other):
        Base(std::move(other))
    {}

    ScopedAsyncUniqueLock& operator=(ScopedAsyncUniqueLock&& rhs)
    {
        Base::operator=(std::move(rhs));

        return *this;
    }

    void swap(ScopedAsyncUniqueLock& other)
    {
        Base::swap(other);
    }
}; // ScopedAsyncUniqueLock<Mutex>

} // detail
} // common
} // mega
