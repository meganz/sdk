#pragma once

#include <cassert>
#include <chrono>
#include <mutex>
#include <utility>

#include <mega/common/lock_forward.h>

namespace mega
{
namespace common
{
namespace detail
{

template<typename T, typename Traits>
struct Lock
{
    using mutex_type = T;

    Lock() = default;

    explicit Lock(mutex_type& mutex)
      : Lock(mutex, std::defer_lock)
    {
        lock();
    }

    Lock(mutex_type& mutex, std::adopt_lock_t)
      : mMutex(&mutex)
      , mOwned(true)
    {
    }

    Lock(mutex_type& mutex, std::defer_lock_t)
      : mMutex(&mutex)
      , mOwned(false)
    {
    }

    Lock(mutex_type& mutex, std::try_to_lock_t)
      : Lock(mutex, std::defer_lock)
    {
        static_cast<void>(try_lock());
    }

    Lock(Lock&& other)
      : mMutex(other.mMutex)
      , mOwned(other.mOwned)
    {
        other.mMutex = nullptr;
        other.mOwned = false;
    }

    ~Lock()
    {
        if (!mOwned)
            return;

        assert(mMutex);

        Traits::unlock(*mMutex);
    }

    operator bool() const
    {
        return owns_lock();
    }

    Lock& operator=(Lock&& rhs)
    {
        Lock temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    void lock()
    {
        assert(mMutex);
        assert(!mOwned);

        Traits::lock(*mMutex);

        mOwned = true;
    }

    mutex_type* mutex() const
    {
        return mMutex;
    }

    bool owns_lock() const
    {
        return mOwned;
    }

    mutex_type* release()
    {
        auto* mutex = mMutex;

        mMutex = nullptr;
        mOwned = false;

        return mutex;
    }

    void swap(Lock& other)
    {
        using std::swap;

        swap(mMutex, other.mMutex);
        swap(mOwned, other.mOwned);
    }

    bool try_lock()
    {
        assert(mMutex);
        assert(!mOwned);

        mOwned = Traits::try_lock(*mMutex);

        return mOwned;
    }

    template<typename Rep, typename Duration>
    bool try_lock_for(std::chrono::duration<Rep, Duration> duration)
    {
        auto now = std::chrono::steady_clock::now();

        return try_lock_until(now + duration);
    }

    bool try_lock_until(std::chrono::steady_clock::time_point time)
    {
        assert(mMutex);
        assert(!mOwned);

        mOwned = Traits::try_lock_until(*mMutex, time);

        return mOwned;
    }

    void unlock()
    {
        assert(mMutex);
        assert(mOwned);

        Traits::unlock(*mMutex);

        mOwned = false;
    }

protected:
    mutex_type* mMutex = nullptr;
    bool mOwned = false;
}; // Lock<T, Traits>

template<typename T>
struct SharedLock
  : Lock<T, SharedLockTraits<T>>
{
    using Traits = SharedLockTraits<T>;
    using Base = Lock<T, Traits>;

    using Base::Base;
    using Base::operator=;

    UniqueLock<T> to_unique_lock()
    {
        assert(this->mMutex);
        assert(this->mOwned);

        Traits::to_unique_lock(*this->mMutex);

        auto result = UniqueLock<T>(*this->mMutex, std::adopt_lock);

        this->mOwned = false;

        return result;
    }

    UniqueLock<T> try_to_unique_lock()
    {
        assert(this->mMutex);
        assert(this->mOwned);

        this->mOwned = !Traits::try_to_unique_lock(*this->mMutex);

        if (!this->mOwned)
            return UniqueLock<T>(*this->mMutex, std::adopt_lock);

        return UniqueLock<T>();
    }

    template<typename Rep, typename Period>
    UniqueLock<T> try_to_unique_lock_for(std::chrono::duration<Rep, Period> duration)
    {
        auto now = std::chrono::steady_clock::now();

        return try_to_unique_lock_until(now + duration);
    }

    UniqueLock<T> try_to_unique_lock_until(std::chrono::steady_clock::time_point time)
    {
        assert(this->mMutex);
        assert(this->mOwned);

        this->mOwned = !Traits::try_to_unique_lock_until(*this->mMutex, time);

        if (!this->mOwned)
            return UniqueLock<T>(*this->mMutex, std::adopt_lock);

        return UniqueLock<T>();
    }
}; // SharedLock<T>

template<typename T>
struct SharedLockTraits
{
    static void lock(T& mutex)
    {
        mutex.lock_shared();
    }

    static void to_unique_lock(T& mutex)
    {
        mutex.to_unique_lock();
    }

    static bool try_lock(T& mutex)
    {
        return mutex.try_lock_shared();
    }

    template<typename TP>
    static bool try_lock_until(T& mutex, TP time)
    {
        return mutex.try_lock_shared_until(time);
    }

    static bool try_to_unique_lock(T& mutex)
    {
        return mutex.try_to_unique_lock();
    }

    template<typename TP>
    static bool try_to_unique_lock_until(T& mutex, TP time)
    {
        return mutex.try_to_unique_lock_until(time);
    }

    static void unlock(T& mutex)
    {
        mutex.unlock_shared();
    }
}; // SharedLockTraits<T>

template<typename T>
struct UniqueLock
  : Lock<T, UniqueLockTraits<T>>
{
    using Traits = UniqueLockTraits<T>;
    using Base = Lock<T, Traits>;

    using Base::Base;
    using Base::operator=;

    SharedLock<T> to_shared_lock()
    {
        assert(this->mMutex);
        assert(this->mOwned);

        Traits::to_shared_lock(*this->mMutex);

        this->mOwned = false;

        return SharedLock<T>(*this->mMutex, std::adopt_lock);
    }
}; // UniqueLock<T>

template<typename T>
struct UniqueLockTraits
{
    static void lock(T& mutex)
    {
        mutex.lock();
    }

    static void to_shared_lock(T& mutex)
    {
        mutex.to_shared_lock();
    }

    static bool try_lock(T& mutex)
    {
        return mutex.try_lock();
    }

    template<typename TP>
    static bool try_lock_until(T& mutex, TP time)
    {
        return mutex.try_lock_until(time);
    }

    static void unlock(T& mutex)
    {
        mutex.unlock();
    }
}; // UniqueLockTraits<T>

} // detail
} // common
} // mega

