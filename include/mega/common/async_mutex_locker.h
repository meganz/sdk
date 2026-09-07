#pragma once

#include <mega/common/async_mutex_forward.h>
#include <mega/common/scoped_async_lock_forward.h>
#include <mega/common/utility.h>

#include <memory>
#include <tuple>
#include <type_traits>

namespace mega
{
namespace common
{
namespace detail
{

// Allows us to acquire a shared lock by calling lock() and tryLock().
template<typename Mutex>
class AsyncSharedLocker final
{
    // Sanity.
    static_assert(IsAsyncMutexV<Mutex>);

    // What mutex do we want to lock?
    Mutex& mMutex;

public:
    explicit AsyncSharedLocker(Mutex& mutex):
        mMutex(mutex)
    {}

    // Acquire a shared lock on mutex.
    template<typename Callback>
    auto lock(Callback&& callback)
    {
        mMutex.lockShared(std::forward<Callback>(callback));
    }

    // Try and acquire a shared lock on mutex.
    auto tryLock()
    {
        return mMutex.tryLockShared();
    }
}; // AsyncSharedLocker<Mutex>

// Allows us to acquire a unique lock by calling lock() and tryLock().
template<typename Mutex>
class AsyncUniqueLocker final
{
    // Sanity.
    static_assert(IsAsyncMutexV<Mutex>);

    // What mutex do we want to lock?
    Mutex& mMutex;

public:
    explicit AsyncUniqueLocker(Mutex& mutex):
        mMutex(mutex)
    {}

    // Acquire a unique lock on mutex.
    template<typename Callback>
    auto lock(Callback&& callback)
    {
        mMutex.lock(std::forward<Callback>(callback));
    }

    // Try and acquire a unique lock on mutex.
    auto tryLock()
    {
        return mMutex.tryLock();
    }
}; // AsyncUniqueLocker<Mutex>

// Test if T is an async locker.
template<typename T>
struct IsAsyncLocker: std::false_type
{}; // IsAsyncLocker<T>

template<typename T>
constexpr auto IsAsyncLockerV = IsAsyncLocker<T>::value;

template<typename Mutex>
struct IsAsyncLocker<AsyncSharedLocker<Mutex>>: std::true_type
{}; // IsAsyncLocker<AsyncSharedLocker<Mutex>>

template<typename Mutex>
struct IsAsyncLocker<AsyncUniqueLocker<Mutex>>: std::true_type
{}; // IsAsyncLocker<AsyncUniqueLocker<Mutex>>

template<typename Locker, typename... Lockers>
using AreAsyncLockers = std::conjunction<IsAsyncLocker<Locker>, IsAsyncLocker<Lockers>...>;

template<typename Locker, typename... Lockers>
constexpr auto AreAsyncLockersV = AreAsyncLockers<Locker, Lockers...>::value;

// Find out what kind of scoped lock Locker::tryLock() returns.
template<typename Locker>
struct GetScopedAsyncLockType
{
    using Type = decltype(std::declval<Locker>().tryLock());
}; // GetScopedAsyncLockType<Locker>

template<typename Locker>
using GetScopedAsyncLockTypeT = typename GetScopedAsyncLockType<Locker>::Type;

// To make logic below simpler.
template<typename Callback, typename Locker, typename... Lockers>
using LockAllCallbackResultT = std::invoke_result_t<Callback,
                                                    GetScopedAsyncLockTypeT<Locker>&&,
                                                    GetScopedAsyncLockTypeT<Lockers>&&...>;

// So we can pass a variable number of lockers to the helper below.
template<typename Locker, typename... Lockers>
struct LockerList
{}; // LockerList<Locker, Lockers>

// True if Callback is a valid lockAll callback.
template<typename Callback, typename Lockers, typename Voider = void>
struct IsLockAllCallbackHelper: std::false_type
{}; // IsLockAllCallbackHelper<Callback, Lockers<Locker, Lockers...>, Voider>

template<typename Callback, typename Locker, typename... Lockers>
struct IsLockAllCallbackHelper<Callback,
                               LockerList<Locker, Lockers...>,
                               std::void_t<LockAllCallbackResultT<Callback, Locker, Lockers...>>>:
    std::is_same<LockAllCallbackResultT<Callback, Locker, Lockers...>, void>
{}; // IsLockAllCallbackHelper<Callback, Locker, Lockers...>

// As above but much more convenient to use.
template<typename Callback, typename Locker, typename... Lockers>
using IsLockAllCallback =
    std::conjunction<AreAsyncLockers<Locker, Lockers...>,
                     IsLockAllCallbackHelper<Callback, LockerList<Locker, Lockers...>>>;

template<typename Callback, typename Locker, typename... Lockers>
constexpr auto IsLockAllCallbackV = IsLockAllCallback<Callback, Locker, Lockers...>::value;

// Tracks state necessary to safely acquire multiple locks.
template<typename Callback, typename... Lockers>
class LockAllContext
{
    using ScopedLocksTuple = std::tuple<GetScopedAsyncLockTypeT<Lockers>...>;

    using LockersTuple = std::tuple<Lockers...>;

    // Called when a lock has been acquired.
    template<typename Lock, auto AcquiredIndex, auto... PendingIndexes>
    void locked(std::shared_ptr<LockAllContext> context,
                Lock&& lock,
                std::index_sequence<AcquiredIndex, PendingIndexes...>)
    {
        // Latch the lock we just acquired.
        std::get<AcquiredIndex>(mScopedLocks) = std::forward<Lock>(lock);

        // Acquire remaining locks.
        tryLock(std::index_sequence<AcquiredIndex>(),
                std::move(context),
                std::index_sequence<PendingIndexes...>());
    }

    // Called to acquire the next pending lock.
    template<auto... AcquiredIndexes, auto PendingIndex, auto... PendingIndexes>
    void tryLock(std::index_sequence<AcquiredIndexes...>,
                 std::shared_ptr<LockAllContext> context,
                 std::index_sequence<PendingIndex, PendingIndexes...>)
    {
        // Try and acquire the next pending lock.
        std::get<PendingIndex>(mScopedLocks) = std::get<PendingIndex>(mLockers).tryLock();

        // Lock was acquired: Acquire the next.
        if (std::get<PendingIndex>(mScopedLocks))
            return tryLock(std::index_sequence<AcquiredIndexes..., PendingIndex>(),
                           std::move(context),
                           std::index_sequence<PendingIndexes...>());

        // Release all of the locks we've acquired so far.
        (std::get<AcquiredIndexes>(mScopedLocks).unlock(), ...);

        // Acquire the next pending lock.
        lock(std::move(context),
             std::index_sequence<PendingIndex, PendingIndexes..., AcquiredIndexes...>());
    }

    template<auto... AcquiredIndexes>
    void tryLock(std::index_sequence<AcquiredIndexes...>,
                 std::shared_ptr<LockAllContext>,
                 std::index_sequence<>)
    {
        // All locks have been acquired.
        std::apply(std::move(mCallback), std::move(mScopedLocks));
    }

    // Who should we call when all locks have been acquired?
    Callback mCallback;

    // The lockers we'll use to populate mScopedLocks.
    LockersTuple mLockers;

    // The scoped locks that will be passed to mCallback.
    ScopedLocksTuple mScopedLocks;

public:
    // Templated for purposes of forwarding.
    template<typename Callback_, typename... Lockers_>
    LockAllContext(Callback_&& callback, Lockers_&&... lockers):
        mCallback(std::forward<Callback_>(callback)),
        mLockers(std::forward<Lockers_>(lockers)...),
        mScopedLocks()
    {}

    template<auto PendingIndex, auto... PendingIndexes>
    void lock(std::shared_ptr<LockAllContext> context,
              std::index_sequence<PendingIndex, PendingIndexes...> pending)
    {
        // Acquire the first pending lock.
        std::get<PendingIndex>(mLockers).lock(
            [context = std::move(context), pending](auto&& lock)
            {
                context->locked(context, std::forward<decltype(lock)>(lock), pending);
            });
    }
}; // LockAllContext<Callback, Lockers...>

// Acquire multiple async locks safely.
template<typename Callback,
         typename Locker,
         typename... Lockers,
         std::enable_if_t<IsLockAllCallbackV<Callback, Locker, Lockers...>>* = nullptr>
auto lockAll(Callback&& callback, Locker&& locker, Lockers&&... lockers)
{
    // Convenience.
    using Context = LockAllContext<Callback, Locker, Lockers...>;

    // Instantiate context.
    auto context = std::make_shared<Context>(std::forward<Callback>(callback),
                                             std::forward<Locker>(locker),
                                             std::forward<Lockers>(lockers)...);

    // Acquire the first lock.
    context->lock(context, std::index_sequence_for<Locker, Lockers...>());
}

// As above but more convenient for synchronous use.
template<typename Locker,
         typename... Lockers,
         std::enable_if_t<AreAsyncLockersV<Locker, Lockers...>>* = nullptr>
auto lockAll(Locker&& locker, Lockers&&... lockers)
{
    // Convenience.
    using Locks = std::tuple<GetScopedAsyncLockTypeT<Locker>, GetScopedAsyncLockTypeT<Lockers>...>;

    // So we can transmit the locks to our waiter.
    auto notifier = makeSharedPromise<Locks>();

    // Acquire the locks.
    lockAll(
        [notifier](auto&&... locks)
        {
            // Transmit locks to our waiter.
            notifier->set_value(std::forward_as_tuple(std::forward<decltype(locks)>(locks)...));
        },
        std::forward<Locker>(locker),
        std::forward<Lockers>(lockers)...);

    // Return the waiter to our caller.
    return notifier->get_future();
}

// Convenience.
template<typename Mutex>
auto sharedLocker(Mutex& mutex) -> std::enable_if_t<IsAsyncMutexV<Mutex>, AsyncSharedLocker<Mutex>>
{
    return AsyncSharedLocker(mutex);
}

template<typename Mutex>
auto uniqueLocker(Mutex& mutex) -> std::enable_if_t<IsAsyncMutexV<Mutex>, AsyncUniqueLocker<Mutex>>
{
    return AsyncUniqueLocker(mutex);
}

} // detail

using detail::AsyncSharedLocker;
using detail::AsyncUniqueLocker;
using detail::lockAll;
using detail::sharedLocker;
using detail::uniqueLocker;

} // common
} // mega
