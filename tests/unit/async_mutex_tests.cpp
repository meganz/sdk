#include <gtest/gtest.h>
#include <mega/common/activity_monitor.h>
#include <mega/common/async_mutex.h>
#include <mega/common/async_mutex_locker.h>
#include <mega/common/concurrent_execution_policy.h>
#include <mega/common/logger.h>
#include <mega/common/scoped_async_lock.h>
#include <mega/common/sequential_execution_policy.h>
#include <mega/common/task_executor.h>
#include <mega/common/utility.h>

#include <chrono>
#include <thread>

namespace mega
{
namespace common
{
namespace testing
{

// Ensures that lock callbacks are invoked on a thread pool.
class ConcurrentExecutionTraits
{
    // Pool used to execute lock callbacks.
    TaskExecutor mExecutor;

public:
    using ExecutionPolicy = ConcurrentExecutionPolicy;

    ConcurrentExecutionTraits():
        mExecutor(TaskExecutorFlags(), logger()),
        mExecutionPolicy(mExecutor)
    {}

    ExecutionPolicy mExecutionPolicy;
}; // ConcurrentExecutionTraits

// Ensures that lock callbacks are invoked directly by the mutex.
struct SequentialExecutionTraits
{
    using ExecutionPolicy = SequentialExecutionPolicy;

    ExecutionPolicy mExecutionPolicy;
}; // SequentialExecutionTraits

// Mutex tests parameterized on callback execution policy.
template<typename ExecutionTraits>
struct AsyncMutexTests: public ::testing::Test, public ExecutionTraits
{
    // Convenience.
    using AsyncMutex = common::AsyncMutex<typename ExecutionTraits::ExecutionPolicy>;

    using ScopedSharedLock = ScopedAsyncSharedLock<AsyncMutex>;
    using ScopedUniqueLock = ScopedAsyncUniqueLock<AsyncMutex>;
}; // AsyncMutexTests<ExecutionTraits>

// The execution policies we want to test against.
using ExecutionTraits = ::testing::Types<SequentialExecutionTraits, ConcurrentExecutionTraits>;

// Instantiate mutex tests for each exeuction policy.
TYPED_TEST_SUITE(AsyncMutexTests, ExecutionTraits);

// Convenience.
using namespace std::literals::chrono_literals;

using std::chrono::steady_clock;

constexpr auto timeout = std::future_status::timeout;

TYPED_TEST(AsyncMutexTests, lock_all_succeeds_with_contention)
{
    // Convenience.
    using ScopedSharedLock = typename TestFixture::ScopedSharedLock;
    using ScopedUniqueLock = typename TestFixture::ScopedUniqueLock;

    AsyncMutex mutex0(this->mExecutionPolicy);
    AsyncMutex mutex1(this->mExecutionPolicy);

    // Don't destroy mutexes until all callbacks have finished executing.
    ActivityMonitor activities;

    // Acquire a unique lock on mutex0.
    auto ulock0 = mutex0.tryLock();
    ASSERT_TRUE(ulock0);

    // Acquire a shared lock on mutex1.
    auto slock1 = mutex1.tryLockShared();
    ASSERT_TRUE(slock1);

    // You should be able to acquire multiple locks.
    auto acquired01 = [&]()
    {
        // So we can tell our waiter when the locks were acquired.
        auto notifier = makeSharedPromise<steady_clock::time_point>();

        // Acquire the locks.
        lockAll(
            [activity = activities.begin(), notifier](auto&& lock0, auto&& lock1)
            {
                // Make sure we were passed the correct type of locks.
                static_assert(std::is_same_v<ScopedSharedLock&&, decltype(lock0)>);
                static_assert(std::is_same_v<ScopedUniqueLock&&, decltype(lock1)>);

                // Both locks were acquired.
                if (lock0 && lock1)
                    return notifier->set_value(steady_clock::now());

                // Couldn't acquire one or more locks.
                notifier->set_value(steady_clock::time_point::min());
            },
            sharedLocker(mutex0),
            uniqueLocker(mutex1));

        // Return waiter to our caller.
        return notifier->get_future();
    }();

    // Release ulock0.
    //
    // This will allow lockAll(...) to acquire a shared lock on mutex0.
    //
    // It will fail to acquire a unique lock on mutex1, however, so it
    // should release its shared lock on mutex0 immediately.
    ulock0.unlock();

    // Reacquire a unique lock on mutex0.
    auto waiter0 = mutex0.lock();
    ASSERT_NE(waiter0.wait_for(64ms), timeout);

    ulock0 = waiter0.get();
    ASSERT_TRUE(ulock0);

    // Release slock1.
    //
    // This will allow lockAll(...) to acquire a unique lock on mutex1.
    //
    // It will fail to acquire a shared lock on mutex0, however, so it
    // should release its unique lock on mutex1 immediately.
    slock1.unlock();

    // Reacquire shared lock on mutex1.
    auto waiter1 = mutex1.lockShared();
    ASSERT_NE(waiter1.wait_for(64ms), timeout);

    slock1 = waiter1.get();
    ASSERT_TRUE(slock1);

    // Release ulock0.
    ulock0.unlock();

    // Release slock1.
    auto released = steady_clock::now();

    slock1.unlock();

    // Wait for lockAll(...) to acquire both locks.
    ASSERT_NE(acquired01.wait_for(64ms), timeout);

    // Make sure lockAll(...) acquired both locks after they were released.
    ASSERT_GE(acquired01.get(), released);
}

TYPED_TEST(AsyncMutexTests, lock_all_succeeds_without_contention)
{
    AsyncMutex mutex0(this->mExecutionPolicy);
    AsyncMutex mutex1(this->mExecutionPolicy);

    {
        // You should be able to acquire a single lock.
        auto waiter = lockAll(uniqueLocker(mutex0));

        // Wait for the lock to be acquired.
        ASSERT_NE(waiter.wait_for(64ms), timeout);

        // Make sure the lock was acquired.
        ASSERT_TRUE(std::get<0>(waiter.get()));
    }

    // You should be able to acquire multiple locks.
    auto waiter = lockAll(sharedLocker(mutex0), uniqueLocker(mutex1));

    // Wait for our two locks to be acquired.
    ASSERT_NE(waiter.wait_for(64ms), timeout);

    // Make sure both locks were acquired.
    auto [lock0, lock1] = waiter.get();

    ASSERT_TRUE(lock0);
    ASSERT_TRUE(lock1);
}

TYPED_TEST(AsyncMutexTests, lock_ordering)
{
    // Convenience.
    using ScopedSharedLock = typename TestFixture::ScopedSharedLock;
    using ScopedUniqueLock = typename TestFixture::ScopedUniqueLock;

    AsyncMutex mutex(this->mExecutionPolicy);

    // Don't destroy mutexes until all callbacks have finished executing.
    ActivityMonitor activities;

    // You should be able to acquire a shared lock.
    auto lock0 = mutex.tryLockShared();
    ASSERT_TRUE(lock0);

    // You should be able to acquire a unique lock but it should be acquired
    // after any shared locks have been released.
    auto released1 = [&]()
    {
        // So we know when the lock was acquired.
        auto notifier = makeSharedPromise<steady_clock::time_point>();

        // Acquire the lock.
        mutex.lock(
            [activity = activities.begin(), notifier](auto&& lock)
            {
                // Make sure the lock is correctly typed.
                static_assert(std::is_same_v<decltype(lock), ScopedUniqueLock&&>);

                // Hold onto the lock for a little while.
                std::this_thread::sleep_for(64ms);

                // Let our waiter know when we released the lock.
                notifier->set_value(steady_clock::now());
            });

        // Return a waiter to our caller.
        return notifier->get_future();
    }();

    // You should be able to acquire a shared lock but it should be acquired
    // after any queued unique locks have been released.
    auto acquired2 = [&]()
    {
        auto notifier = makeSharedPromise<steady_clock::time_point>();

        mutex.lockShared(
            [activity = activities.begin(), notifier](auto&& lock)
            {
                static_assert(std::is_same_v<decltype(lock), ScopedSharedLock&&>);

                notifier->set_value(steady_clock::now());
            });

        return notifier->get_future();
    }();

    // Release the first lock.
    lock0.unlock();

    // Wait for the second lock to be released.
    ASSERT_NE(released1.wait_for(2s), timeout);

    // The third lock should be acquired after the second has been released.
    ASSERT_NE(acquired2.wait_for(2s), timeout);
    ASSERT_GE(acquired2.get(), released1.get());
}

TYPED_TEST(AsyncMutexTests, lock_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a unique lock.
    auto waiter = mutex.lock();
    ASSERT_NE(waiter.wait_for(64ms), timeout);
    ASSERT_TRUE(waiter.get());
}

TYPED_TEST(AsyncMutexTests, lock_shared_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a shared lock.
    auto waiter = mutex.lockShared();
    ASSERT_NE(waiter.wait_for(64ms), timeout);
    ASSERT_TRUE(waiter.get());
}

TYPED_TEST(AsyncMutexTests, to_unique_lock_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a shared lock on a mutex.
    //
    // Acquisition is immediate as the mutex is not held by any lock.
    auto slock0 = mutex.tryLockShared();
    ASSERT_TRUE(slock0);

    // You should be able to acquire multiple shared locks on a mutex.
    //
    // Acquisition is immediate as the mutex is not held by any unique lock.
    auto slock1 = mutex.tryLockShared();
    ASSERT_TRUE(slock1);

    // You should be able to acquire a unique lock on a mutex.
    //
    // Acquisition will be deferred until all shared locks are released.
    auto ulock0w = mutex.lock();

    // You should be able to convert a shared lock to a unique lock.
    //
    // Acquisition will be deferred until all other shared locks have been released.
    //
    // This acquisition will be prioritized above ulock0.
    auto ulock1w = slock0.toUniqueLock();

    // Release slock1.
    //
    // This will allow the acquisition of ulock1.
    slock1.unlock();

    // Wait until slock0 has been converted to a unique lock.
    ASSERT_NE(ulock1w.wait_for(64ms), timeout);
    ASSERT_TRUE(ulock1w.get());

    // Wait until ulock0 as been acquired.
    ASSERT_NE(ulock0w.wait_for(64ms), timeout);
    ASSERT_TRUE(ulock0w.get());
}

TYPED_TEST(AsyncMutexTests, try_to_unique_lock_fails)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a shared lock.
    auto lock0 = mutex.tryLockShared();
    ASSERT_TRUE(lock0);

    // Even if another shared lock has been acquired.
    auto lock1 = mutex.tryLockShared();
    ASSERT_TRUE(lock1);

    // But you should only be able to convert a shared lock to a unique lock
    // if no other shared locks are held.
    ASSERT_FALSE(lock0.tryToUniqueLock());
}

TYPED_TEST(AsyncMutexTests, try_to_unique_lock_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a shared lock.
    auto lock0 = mutex.tryLockShared();
    ASSERT_TRUE(lock0);

    // And you should be able to convert that lock into a unique lock.
    ASSERT_TRUE(lock0.tryToUniqueLock());
}

TYPED_TEST(AsyncMutexTests, try_lock_fails)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    {
        // You should be able to acquire a unique lock.
        auto lock = mutex.tryLock();
        ASSERT_TRUE(lock);

        // Unless a unique lock has already been acquired on the mutex.
        ASSERT_FALSE(mutex.tryLock());
    }

    // You should be able to acquire a shared lock.
    auto lock = mutex.tryLockShared();
    ASSERT_TRUE(lock);

    // But not a unique lock if a shared lock has already been acquired.
    ASSERT_FALSE(mutex.tryLock());
}

TYPED_TEST(AsyncMutexTests, try_lock_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a unique lock.
    ASSERT_TRUE(mutex.tryLock());
}

TYPED_TEST(AsyncMutexTests, try_lock_shared_fails)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a unique lock on a mutex.
    auto lock = mutex.tryLock();
    ASSERT_TRUE(lock);

    // But not a shared lock if a unique lock has already been acquired.
    ASSERT_FALSE(mutex.tryLockShared());
}

TYPED_TEST(AsyncMutexTests, try_lock_shared_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // You should be able to acquire a shared lock.
    auto lock0 = mutex.tryLockShared();
    ASSERT_TRUE(lock0);

    // Even if a shared lock has already been acquired.
    ASSERT_TRUE(mutex.tryLockShared());
}

TYPED_TEST(AsyncMutexTests, unlock_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // Don't destroy mutexes until all callbacks have finished executing.
    ActivityMonitor activities;

    // Acquire a unique lock.
    auto lock0 = mutex.tryLock();
    ASSERT_TRUE(lock0);

    // Try and acquire another unique lock.
    auto acquired1 = [&]()
    {
        // So we can signal when the lock was acquired.
        auto notifier = makeSharedPromise<steady_clock::time_point>();

        // Try and acquire the lock.
        mutex.lock(
            [activity = activities.begin(), notifier](auto)
            {
                // Let our waiter know the lock was acquired.
                notifier->set_value(steady_clock::now());

                // Wait for a little while before we release the lock.
                std::this_thread::sleep_for(64ms);
            });

        // Return the waiter to our caller.
        return notifier->get_future();
    }();

    // Try and acquire a shared lock.
    auto acquired2 = [&]()
    {
        auto notifier = makeSharedPromise<steady_clock::time_point>();

        mutex.lockShared(
            [activity = activities.begin(), notifier](auto)
            {
                notifier->set_value(steady_clock::now());
            });

        return notifier->get_future();
    }();

    // Release our first lock.
    auto released0 = steady_clock::now();

    lock0.unlock();

    // Make sure our second lock was acquired after we released the first.
    ASSERT_NE(acquired1.wait_for(2s), timeout);
    ASSERT_GE(acquired1.get(), released0);

    // Make sure the third lock was acquired some time after the second.
    ASSERT_NE(acquired2.wait_for(2s), timeout);
    ASSERT_GE(acquired2.get(), released0 + 64ms);
}

TYPED_TEST(AsyncMutexTests, unlock_shared_succeeds)
{
    AsyncMutex mutex(this->mExecutionPolicy);

    // Don't destroy mutexes until all callbacks have finished executing.
    ActivityMonitor activities;

    // Acquire a shared lock.
    auto lock0 = mutex.tryLockShared();
    ASSERT_TRUE(lock0);

    // Acquire another shared lock.
    auto lock1 = mutex.tryLockShared();
    ASSERT_TRUE(lock1);

    // Acquire a unique lock.
    auto acquired2 = [&]()
    {
        // So we know when the lock was acquired.
        auto notifier = makeSharedPromise<steady_clock::time_point>();

        // Acquire a unique lock.
        mutex.lock(
            [activity = activities.begin(), notifier](auto)
            {
                // Let our waiter know that we've acquired the lock.
                notifier->set_value(steady_clock::now());
            });

        // Return waiter to our caller.
        return notifier->get_future();
    }();

    // Release the first lock.
    lock0.unlock();

    // Wait a little while.
    std::this_thread::sleep_for(64ms);

    // Release the second lock.
    auto released1 = steady_clock::now();

    lock1.unlock();

    // Make sure the unique lock was acquired after both shared locks were
    // released.
    ASSERT_NE(acquired2.wait_for(2s), timeout);
    ASSERT_GE(acquired2.get(), released1);
}

} // testing
} // common
} // mega
