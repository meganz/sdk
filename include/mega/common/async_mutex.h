#pragma once

#include <mega/common/assertions.h>
#include <mega/common/async_mutex_forward.h>
#include <mega/common/move_only_function.h>
#include <mega/common/scoped_async_lock.h>
#include <mega/common/utility.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

namespace mega
{
namespace common
{
namespace detail
{

// Convenience.
template<typename Callback, template<typename> typename Lock, typename Mutex>
using LockCallbackResultT = std::invoke_result_t<Callback, Lock<Mutex>&&>;

// True if callback is a valid lock callback.
template<typename Callback, template<typename> typename Lock, typename Mutex, typename Voider>
struct IsLockCallback: std::false_type
{}; // IsLockCallback<Callback, Lock<Mutex>, Mutex>, Voider>

template<typename Callback, template<typename> typename Lock, typename Mutex>
struct IsLockCallback<Callback,
                      Lock,
                      Mutex,
                      std::void_t<LockCallbackResultT<Callback, Lock, Mutex>>>:
    std::is_same<LockCallbackResultT<Callback, Lock, Mutex>, void>
{}; // IsLockCallback<Callback, Lock<Mutex>, Mutex>, T>

// Convenience.
using SignedSizeT = std::make_signed_t<std::size_t>;

// Implements a mutex that doesn't block the calling thread.
//
// There are times when we want to perform synchronization on different
// threads but don't want to lose control of the thread's flow of execution.
//
// That is, we don't want the locking thread to block because we might be
// able to perform other work in the mean time.
//
// This class differs from std::mutex because it allows a thread to specify
// a callback that should be executed when a lock has been acquired.
//
// There are some details to keep in mind when using this class, however:
//
// 1. When a caller can acquire a lock without waiting, that caller's
//    callback is executed immediately.
//
// 2. Queued callbacks are executed when a lock is released. Note that these
//    callbacks are, by default, executed on the stack of the releaser.
//
// 3. Callbacks are executed indirectly through an execution policy with is
//    provided to the mutex upon construction.
//
//    An execution policy must implement two template methods:
//
//    - dispatch(Callback&&, Parameters&&...)
//    - post(Callback&&, Parameters&&...)
//
//    dispatch(...) is used by the mutex to invoke a callback on the current
//    thread. An example would be when a lock can be acquired by teh caller
//    immediately without queuing.
//
//    post(...) is used by the mutex to schedule execution of a callback on
//    a potentially different thread. An example would be when executing
//    queued callbacks when a lock is released.
//
// 4. The intention of an execution policy is to allow us to optimize
//    callback execution in the future if necessary.
template<typename ExecutionPolicy>
class AsyncMutex final
{
    // So PendingLockAcquisition can declare toSharedLockAcquisition().
    class PendingSharedLockAcquisition;

    // Represents a lock waiting to acquire the mutex.
    class PendingLockAcquisition
    {
    protected:
        PendingLockAcquisition() = default;

    public:
        virtual ~PendingLockAcquisition() = default;

        // Called when the mutex has been acquired.
        virtual void acquired(AsyncMutex& mutex) = 0;

        // Called when the acquisition has been dequeued from mutex.
        virtual void dequeued(std::unique_lock<std::mutex>&, AsyncMutex&) {}

        // Called when the acquisition has been queued on mutex.
        virtual void queued(std::unique_lock<std::mutex>&, AsyncMutex&) {}

        // Dynamically cast this acquisition to a shared lock acquisition.
        virtual PendingSharedLockAcquisition* toSharedLockAcquisition() = 0;
    }; // PendingLockAcquisition<Mutex>

    // Convenience.
    using PendingLockAcquisitionPtr = std::unique_ptr<PendingLockAcquisition>;
    using PendingLockAcquisitionPtrList = std::list<PendingLockAcquisitionPtr>;

    // Represents one or more shared locks waiting to acquire the mutex.
    class PendingSharedLockAcquisition final: public PendingLockAcquisition
    {
        // Convenience.
        using ScopedLock = ScopedAsyncSharedLock<AsyncMutex>;
        using Callback = MoveOnlyFunction<void(ScopedLock)>;
        using CallbackList = std::list<Callback>;

        // Which callbacks should we invoke when the mutex has been acquired?
        CallbackList mCallbacks;

    public:
        PendingSharedLockAcquisition():
            PendingLockAcquisition(),
            mCallbacks()
        {}

        // Called when the mutex has been acquired.
        void acquired(AsyncMutex& mutex) override
        {
            // Invoke each lock callback.
            for (auto& callback: mCallbacks)
                mutex.mExecutionPolicy.post(std::move(callback), mutex.sharedLock());
        }

        // Called when the acquisition has been dequeued from mutex.
        void dequeued([[maybe_unused]] std::unique_lock<std::mutex>& lock,
                      AsyncMutex& mutex) override
        {
            // Sanity.
            ASSERT_ALWAYS(lock.owns_lock());
            ASSERT_ALWAYS(lock.mutex() == &mutex.mLock);
            ASSERT_ALWAYS(mutex.mState >= 0);

            // Let the mutex know that it's now owned by N shared locks.
            mutex.mState += static_cast<SignedSizeT>(mCallbacks.size());
        }

        // Queue a callback for execution when the mutex is acquired.
        template<typename EffectiveCallback>
        auto queue(EffectiveCallback&& callback)
            -> std::enable_if_t<IsSharedLockCallbackV<Callback, AsyncMutex>>
        {
            mCallbacks.emplace_back(std::forward<EffectiveCallback>(callback));
        }

        // Dynamically cast this acquisition to a shared lock acquisition.
        PendingSharedLockAcquisition* toSharedLockAcquisition() override
        {
            return this;
        }
    }; // PendingSharedAcquisition

    // Represents a unique lock waiting to acquire the mutex.
    template<typename Callback>
    class PendingUniqueLockAcquisition: public PendingLockAcquisition
    {
        // Sanity.
        static_assert(IsUniqueLockCallbackV<Callback, AsyncMutex>);

        // Convenience.
        using ScopedLock = ScopedAsyncUniqueLock<AsyncMutex>;

        // What callback should we invoke when the mutex has been acquired?
        Callback mCallback;

    public:
        template<typename EffectiveCallback,
                 std::enable_if_t<IsUniqueLockCallbackV<Callback, AsyncMutex>>* = nullptr>
        PendingUniqueLockAcquisition(EffectiveCallback&& callback):
            mCallback(std::forward<EffectiveCallback>(callback))
        {}

        virtual ~PendingUniqueLockAcquisition() = default;

        // Called when the mutex has been acquired.
        void acquired(AsyncMutex& mutex) override
        {
            // Invoke the callback.
            mutex.mExecutionPolicy.post(std::move(mCallback), mutex.uniqueLock());
        }

        // Called when the acquisition has been dequeued from mutex.
        void dequeued([[maybe_unused]] std::unique_lock<std::mutex>& lock,
                      AsyncMutex& mutex) override
        {
            // Sanity.
            ASSERT_ALWAYS(lock.owns_lock());
            ASSERT_ALWAYS(lock.mutex() == &mutex.mLock);
            ASSERT_ALWAYS(mutex.mState == 0);

            // Let the mutex know that it's now owned by a unique lock.
            --mutex.mState;
        }

        // Dynamically cast this acquisition to a shared lock acquisition.
        PendingSharedLockAcquisition* toSharedLockAcquisition() override
        {
            return nullptr;
        }
    }; // PendingUniqueLockAcquisition<Callback>

    // Represents a shared lock waiting to be promoted to a unique lock.
    template<typename Callback>
    class PendingSharedLockPromotion final: public PendingUniqueLockAcquisition<Callback>
    {
        // Convenience.
        using Base = PendingUniqueLockAcquisition<Callback>;

    public:
        template<typename EffectiveCallback,
                 std::enable_if_t<IsUniqueLockCallbackV<Callback, AsyncMutex>>* = nullptr>
        PendingSharedLockPromotion(EffectiveCallback&& callback):
            Base(std::forward<EffectiveCallback>(callback))
        {}

        // Called when the acquisition has been dequeued from mutex.
        void dequeued([[maybe_unused]] std::unique_lock<std::mutex>& lock,
                      AsyncMutex& mutex) override
        {
            // Sanity.
            ASSERT_ALWAYS(lock.owns_lock());
            ASSERT_ALWAYS(lock.mutex() == &mutex.mLock);

            // Make sure we execute our base class's logic.
            Base::dequeued(lock, mutex);

            // Let the mutex know that the shared lock promotion has completed.
            mutex.mPendingSharedLockPromotion = nullptr;
        }

        // Called when the acquisition has been queued on mutex.
        void queued([[maybe_unused]] std::unique_lock<std::mutex>& lock, AsyncMutex& mutex) override
        {
            // Sanity.
            ASSERT_ALWAYS(lock.owns_lock());
            ASSERT_ALWAYS(lock.mutex() == &mutex.mLock);
            ASSERT_ALWAYS(mutex.mState > 0);

            // Make sure we execute our base class's logic.
            Base::queued(lock, mutex);

            // Calling lock no longer owns the mutex.
            --mutex.mState;
        }
    }; // PendingSharedLockPromotion<Callback>

    // Convenience.
    using ScopedSharedLock = detail::ScopedAsyncSharedLock<AsyncMutex>;
    using ScopedUniqueLock = detail::ScopedAsyncUniqueLock<AsyncMutex>;

    // So ScopedSharedLock can invoke unlockShared().
    friend ScopedSharedLock;

    // So ScopedUniqueLock can invoke unlock();
    friend ScopedUniqueLock;

    // Dequeue a pending lock acquisition.
    template<typename Lock>
    PendingLockAcquisitionPtr dequeue(Lock&& lock)
    {
        // Sanity.
        ASSERT_ALWAYS(lock.mutex() == &mLock);
        ASSERT_ALWAYS(lock.owns_lock());

        // There are no pending acquisitions.
        if (mPendingLockAcquisitions.empty())
            return nullptr;

        // Dequeue the acquisition.
        auto acquisition = std::move(mPendingLockAcquisitions.front());

        mPendingLockAcquisitions.pop_front();

        // Let the acquisition know that it's been dequeued.
        acquisition->dequeued(lock, *this);

        // Return the acquisition to our caller.
        return acquisition;
    }

    // Relinquishes a lock on this mutex.
    void doUnlock(int adjustment)
    {
        // Acquire mutex lock.
        std::unique_lock lock(mLock);

        // Sanity: adjustment must be either -1 or +1.
        ASSERT_ALWAYS(std::abs(adjustment) == 1);

        // Sanity: mutex must be locked.
        ASSERT_ALWAYS((adjustment == 1 && mState < 0) || (adjustment == -1 && mState > 0));

        // The lock on longer owns this mutex.
        mState += adjustment;

        // Mutex is still owned by one or more locks.
        if (mState)
            return;

        // Try and pop an acquisition from the queue.
        auto acquisition = dequeue(lock);

        // There are no pending lock acquisitions.
        if (!acquisition)
            return;

        // Release mutex lock.
        lock.unlock();

        // Execute pending lock acqusition.
        acquisition->acquired(*this);
    }

    // Queue a pending acquisition on this mutex.
    template<typename AcquisitionPtr, typename Lock, typename Position>
    auto queue(AcquisitionPtr&& acquisition, [[maybe_unused]] Lock&& lock, Position position)
    {
        // Sanity.
        ASSERT_ALWAYS(acquisition);
        ASSERT_ALWAYS(lock.mutex() == &mLock);
        ASSERT_ALWAYS(lock.owns_lock());

        // Let the acquisition know that it's being queued.
        acquisition->queued(lock, *this);

        // Save a reference to the acquisition to return to our caller.
        auto result = acquisition.get();

        // Queue the acquisition.
        mPendingLockAcquisitions.emplace(position, std::forward<AcquisitionPtr>(acquisition));

        // Return the acquisition to our caller.
        return result;
    }

    // Create a new shared lock on this mutex.
    auto sharedLock() -> ScopedSharedLock
    {
        return ScopedSharedLock(*this, std::adopt_lock);
    }

    // Called to promote an existing shared lock into a unique lock.
    template<typename Callback>
    auto toUniqueLock(Callback&& callback)
        -> std::enable_if_t<IsUniqueLockCallbackV<Callback, AsyncMutex>>
    {
        // Make sure no one else is messing with this mutex.
        std::unique_lock guard(mLock);

        // Sanity: Make sure we're not already promoting a shared lock.
        ASSERT_ALWAYS(!mPendingSharedLockPromotion);

        // Calling lock is the sole owner of this mutex.
        if (auto lock = tryToUniqueLock(guard))
        {
            // Release mutex lock.
            guard.unlock();

            // Apply the caller's callback to the promoted lock.
            return std::invoke(std::forward<Callback>(callback), std::move(lock));
        }

        // Convenience.
        using Acquisition = PendingSharedLockPromotion<Callback>;

        // Add the acquisition to the front of the queue.
        mPendingSharedLockPromotion =
            queue(std::make_unique<Acquisition>(std::forward<Callback>(callback)),
                  guard,
                  mPendingLockAcquisitions.cbegin());
    }

    // Try and acquire a unique lock on this mutex.
    template<typename Lock>
    auto tryLock([[maybe_unused]] Lock&& lock) -> ScopedUniqueLock
    {
        // Sanity.
        ASSERT_ALWAYS(lock.mutex() == &mLock);
        ASSERT_ALWAYS(lock.owns_lock());

        // One or more locks already own this mutex.
        if (mState)
            return ScopedUniqueLock(*this, std::defer_lock);

        // Mutex is now owned by a unique lock.
        --mState;

        // Return unique lock to caller.
        return ScopedUniqueLock(*this, std::adopt_lock);
    }

    // Try and acquire a shared lock on this mutex.
    template<typename Lock>
    auto tryLockShared([[maybe_unused]] Lock&& lock) -> ScopedSharedLock
    {
        // Sanity.
        ASSERT_ALWAYS(lock.owns_lock());
        ASSERT_ALWAYS(lock.mutex() == &mLock);

        // One or more lock acquisitions have already been queued.
        //
        // This is important as it ensures that shared locks are always
        // acquired after an earlier unique lock.
        //
        // Imagine this case:
        // Thread 0: Acquires a shared lock.
        // Thread 1: Tries to acquire a unique lock and is queued.
        // Thread 2: Tries to acquire a shared lock.
        //
        // Given that Thread 0 owns a shared lock on the mutex, Thread 2
        // could also acquire its shared lock immediately but if we did
        // that, it would be out of order with respect to the previously
        // queued unique lock acquisition.
        if (!mPendingLockAcquisitions.empty())
            return ScopedSharedLock(*this, std::defer_lock);

        // Mutex is already owned by a unique lock.
        if (mState < 0)
            return ScopedSharedLock(*this, std::defer_lock);

        // Mutex is now owned by a shared lock.
        ++mState;

        // Return shared lock to our caller.
        return ScopedSharedLock(*this, std::adopt_lock);
    }

    // Try and promote an existing shared lock into a unique lock.
    template<typename Lock>
    auto tryToUniqueLock([[maybe_unused]] Lock&& lock) -> ScopedUniqueLock
    {
        // Sanity.
        ASSERT_ALWAYS(lock.mutex() == &mLock);
        ASSERT_ALWAYS(lock.owns_lock());

        // The mutex is either:
        // - Already promoting a shared lock.
        // - Held by other shared locks.
        if (mPendingSharedLockPromotion || mState > 1)
            return ScopedUniqueLock(*this, std::defer_lock);

        // Promote shared lock to a unique lock.
        mState = -1;

        // Return unique lock to our caller.
        return ScopedUniqueLock(*this, std::adopt_lock);
    }

    auto tryToUniqueLock() -> ScopedUniqueLock
    {
        return tryToUniqueLock(std::unique_lock(mLock));
    }

    // Create a new unique lock on this mutex.
    auto uniqueLock() -> ScopedUniqueLock
    {
        return ScopedUniqueLock(*this, std::adopt_lock);
    }

    // Release a unique lock on this mutex.
    void unlock()
    {
        doUnlock(1);
    }

    // Release a shared lock on this mutex.
    void unlockShared()
    {
        doUnlock(-1);
    }

    // Controls how lock callbacks are executed.
    ExecutionPolicy mExecutionPolicy;

    // Serializes access to our members.
    std::mutex mLock;

    // List of locks waiting to be acquired.
    PendingLockAcquisitionPtrList mPendingLockAcquisitions;

    // Current shared lock waiting to be promoted, if any.
    PendingLockAcquisition* mPendingSharedLockPromotion = nullptr;

    // The current state of the mutex.
    //
    // <0 - A unique lock owns this mutex.
    // =0 - No locks own this mutex.
    // >0 - One or more shared locks own this mutex.
    SignedSizeT mState;

public:
    AsyncMutex(ExecutionPolicy executionPolicy = ExecutionPolicy()):
        mExecutionPolicy(std::move(executionPolicy)),
        mLock(),
        mPendingLockAcquisitions(),
        mState(0)
    {}

    AsyncMutex(AsyncMutex&& other) = delete;

    AsyncMutex& operator=(AsyncMutex&& rhs) = delete;

    ~AsyncMutex()
    {
        std::lock_guard guard(mLock);

        // Sanity: mutex must have no pending lock acquisitions.
        ASSERT_ALWAYS(mPendingLockAcquisitions.empty());
        ASSERT_ALWAYS(mPendingSharedLockPromotion == nullptr);

        // Sanity: mutex must not be locked.
        ASSERT_ALWAYS(mState == 0);
    }

    // Acquire a unique lock on this mutex.
    //
    // When the mutex has been acquired, callback will be passed an
    // ScopedAsyncUniqueLock instance.
    template<typename Callback>
    auto lock(Callback&& callback) -> std::enable_if_t<IsUniqueLockCallbackV<Callback, AsyncMutex>>
    {
        // Make sure no one else is messing with this mutex.
        std::unique_lock guard(mLock);

        // No lock owns this mutex.
        if (auto lock = tryLock(guard))
        {
            // Release the mutex lock.
            guard.unlock();

            // Apply the caller's callback to a scoped unique lock.
            return std::invoke(std::forward<Callback>(callback), std::move(lock));
        }

        // Convenience.
        using Acquisition = PendingUniqueLockAcquisition<Callback>;

        // Add the acquisition to our queue.
        queue(std::make_unique<Acquisition>(std::forward<Callback>(callback)),
              guard,
              mPendingLockAcquisitions.cend());
    }

    // Acquire a unique lock on this mutex.
    auto lock() -> std::future<ScopedUniqueLock>
    {
        // So we can transmit the lock to our waiter.
        std::promise<ScopedUniqueLock> notifier;

        // So we can wait until the unique lock has been acquired.
        auto waiter = notifier.get_future();

        // Acquire the unique lock.
        lock(
            [notifier = std::move(notifier)](auto&& lock) mutable
            {
                notifier.set_value(std::forward<decltype(lock)>(lock));
            });

        // Return waiter to our caller.
        return waiter;
    }

    // Acquire a shared lock on this mutex.
    //
    // When the mutex has been acquired, callback will be passed an
    // ScopedAsyncSharedLock instance.
    template<typename Callback>
    auto lockShared(Callback&& callback)
        -> std::enable_if_t<IsSharedLockCallbackV<Callback, AsyncMutex>>
    {
        // Make sure no one else is messing with this mutex.
        std::unique_lock guard(mLock);

        // No unique lock owns this mutex.
        if (auto lock = tryLockShared(guard))
        {
            // Release mutex lock.
            guard.unlock();

            // Apply caller's callback to our shared lock.
            return std::invoke(std::forward<Callback>(callback), std::move(lock));
        }

        // Assume last queued acquisition is not for a shared lock.
        PendingSharedLockAcquisition* acquisition = nullptr;

        // Queue isn't empty: Check if last queued acquisition is for a shared lock.
        if (!mPendingLockAcquisitions.empty())
            acquisition = mPendingLockAcquisitions.back()->toSharedLockAcquisition();

        // Queue a new shared lock acquisition if necessary.
        if (!acquisition)
            acquisition = queue(std::make_unique<PendingSharedLockAcquisition>(),
                                guard,
                                mPendingLockAcquisitions.cend());

        // Queue the caller's callback for later execution.
        acquisition->queue(std::forward<Callback>(callback));
    }

    // Acquire a shared lock on this mutex.
    auto lockShared() -> std::future<ScopedSharedLock>
    {
        // So we can transmit the lock to our waiter.
        std::promise<ScopedSharedLock> notifier;

        // So we can wait until the shared lock has been acquired.
        auto waiter = notifier.get_future();

        // Acquire the unique lock.
        lockShared(
            [notifier = std::move(notifier)](auto&& lock) mutable
            {
                notifier.set_value(std::forward<decltype(lock)>(lock));
            });

        // Return waiter to our caller.
        return waiter;
    }

    // Try and acquire a unique lock on this mutex.
    auto tryLock() -> ScopedUniqueLock
    {
        return tryLock(std::unique_lock(mLock));
    }

    // Try and acquire a shared lock on this mutex.
    auto tryLockShared() -> ScopedSharedLock
    {
        return tryLockShared(std::unique_lock(mLock));
    }
}; // AsyncMutex<ExecutionPolicy>

} // detail
} // common
} // mega
