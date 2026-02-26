#pragma once

#include <cassert>
#include <mutex>
#include <utility>

#include <mega/common/type_traits.h>
#include <mega/fuse/common/any_lock_forward.h>

namespace mega
{
namespace fuse
{

class AnyLock
{
    struct OperationsBase
    {
        // Acquire this lock.
        virtual void lock(void* lock) const = 0;

        // Try and acquire this lock.
        virtual bool try_lock(void* lock) const = 0;

        // Release this lock.
        virtual void unlock(void* lock) const = 0;
    }; // OperationsBase

    template<typename T>
    struct Operations
      : public OperationsBase
    {
        // Acquire this lock.
        void lock(void* lock) const override
        {
            to_lock(lock).lock();
        }

        // Retrieve typed reference to specified lock.
        T& to_lock(void* lock) const
        {
            return *reinterpret_cast<T*>(lock);
        }

        // Try and acquire this lock.
        bool try_lock(void* lock) const override
        {
            return to_lock(lock).try_lock();
        }

        // Release this lock.
        void unlock(void* lock) const override
        {
            to_lock(lock).unlock();
        }
    }; // Operations<T>

    // Retrieve operations for a specific lock type.
    template<typename T>
    static const OperationsBase* operations()
    {
        static const Operations<T> operations;

        return &operations;
    }

    // What lock are we currently wrapping?
    void* mLock;

    // How do we operate on that lock?
    const OperationsBase* mOperations;

    // Do we own this lock?
    bool mOwned;

public:
    AnyLock()
      : mLock(nullptr)
      , mOperations(nullptr)
      , mOwned(false)
    {
    }

    template<typename T>
    AnyLock(const T& lock, std::defer_lock_t defer)
      : AnyLock(const_cast<T&>(lock), defer)
    {
    }

    template<typename T>
    AnyLock(T& lock, std::defer_lock_t)
      : mLock(reinterpret_cast<void*>(&lock))
      , mOperations(operations<T>())
      , mOwned(false)
    {
    }

    template<typename T>
    AnyLock(T& lock)
      : AnyLock(lock, std::defer_lock)
    {
        // Acquire the lock.
        this->lock();
    }

    AnyLock(AnyLock&& other)
      : mLock(std::move(other.mLock))
      , mOperations(std::move(other.mOperations))
      , mOwned(other.mOwned)
    {
        other.mLock = nullptr;
        other.mOperations = nullptr;
        other.mOwned = false;
    }

    ~AnyLock()
    {
        if (mOwned)
            unlock();
    }

    // Do we own this lock?
    operator bool() const
    {
        return owns_lock();
    }

    AnyLock& operator=(AnyLock&& rhs)
    {
        AnyLock temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    // Acquire this lock.
    void lock()
    {
        assert(mLock);
        assert(mOperations);
        assert(!mOwned);

        mOperations->lock(mLock);

        mOwned = true;
    }

    // Do we currently own this lock?
    bool owns_lock() const
    {
        return mOwned;
    }

    // Release ownership of this lock.
    void release()
    {
        mLock = nullptr;
        mOperations = nullptr;
        mOwned = false;
    }

    // Swap this lock with another.
    void swap(AnyLock& other)
    {
        using std::swap;

        swap(mLock, other.mLock);
        swap(mOperations, other.mOperations);
        swap(mOwned, other.mOwned);
    }

    // Try and acquire this lock.
    bool try_lock()
    {
        assert(mLock);
        assert(mOperations);

        mOwned = mOperations->try_lock(mLock);

        return mOwned;
    }

    // Release this lock.
    void unlock()
    {
        assert(mLock);
        assert(mOperations);
        assert(mOwned);

        mOperations->unlock(mLock);

        mOwned = false;
    }
}; // AnyLock

} // fuse
} // mega

