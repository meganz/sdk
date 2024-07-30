#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include <mega/fuse/common/lock.h>
#include <mega/fuse/common/shared_mutex.h>

namespace mega
{
namespace fuse
{
namespace testing
{

// Convenience.
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
using TimePoint = Clock::time_point;

class FUSESharedMutexTests
  : public ::testing::Test
{
    // Signaled when a function has completed execution.
    std::condition_variable mCV;

    // Serializes access to instance members.
    std::mutex mLock;

    // How many functions are currently being executed?
    std::uint64_t mNumFunctions;

public:
    FUSESharedMutexTests()
      : Test()
      , mCV()
      , mLock()
      , mNumFunctions(0)
    {
    }

    ~FUSESharedMutexTests()
    {
        std::unique_lock<std::mutex> lock(mLock);

        // Wait for queued functions to complete.
        mCV.wait(lock, [&]() { return !mNumFunctions; });
    }

    // Queue a function for execution on another thread.
    template<typename Ret>
    std::future<Ret> execute(std::function<Ret()> function)
    {
        std::lock_guard<std::mutex> guard(mLock);

        // Wrap the caller's function.
        auto wrapper = [this](std::function<Ret()>& function) {
            // Execute the caller's function.
            auto result = function();

            std::lock_guard<std::mutex> guard(mLock);

            // Notify the fixture that the function's completed.
            --mNumFunctions;

            // Wake the fixture if necessary.
            mCV.notify_one();

            // Return result to caller.
            return result;
        }; // wrapper

        function = std::bind(std::move(wrapper), std::move(function));

        // Package the task for execution.
        auto task = std::packaged_task<Ret()>(std::move(function));

        // Retrieve future so the caller can wait for the function's result.
        auto future = task.get_future();

        // Remember that we've queued a function for execution.
        ++mNumFunctions;

        // Spawn a thread to execute the task.
        std::thread thread(std::move(task));

        // Detach the thread so we can return immediately.
        thread.detach();

        // Return the future to the caller.
        return future;
    }
}; // FUSESharedMutexTests

TEST_F(FUSESharedMutexTests, lock_fails)
{
    SharedMutex mutex;

    {
        UniqueLock<SharedMutex> lock(mutex, std::try_to_lock);
        ASSERT_TRUE(lock);

        auto result = execute(std::function<bool()>([&]() {
            return !UniqueLock<SharedMutex>(mutex, std::try_to_lock);
        }));

        ASSERT_TRUE(result.get());
    }

    SharedLock<SharedMutex> lock(mutex, std::try_to_lock);
    ASSERT_TRUE(lock);

    auto result = execute(std::function<bool()>([&]() {
        return !UniqueLock<SharedMutex>(mutex, std::try_to_lock);
    }));

    ASSERT_TRUE(result.get());
}

TEST_F(FUSESharedMutexTests, lock_recursive_succeeds)
{
    SharedMutex mutex;

    UniqueLock<SharedMutex> lock0(mutex, std::try_to_lock);
    ASSERT_TRUE(lock0);

    UniqueLock<SharedMutex> lock1(mutex, std::try_to_lock);
    ASSERT_TRUE(lock1);
}

TEST_F(FUSESharedMutexTests, lock_succeeds)
{
    SharedMutex mutex;

    UniqueLock<SharedMutex> lock(mutex, std::try_to_lock);
    ASSERT_TRUE(lock);

    auto result = execute(std::function<TimePoint()>([&]() {
        UniqueLock<SharedMutex> lock(mutex, std::defer_lock);

        if (lock.try_lock_for(Milliseconds(256)))
            return Clock::now();

        return TimePoint::min();
    }));

    std::this_thread::sleep_for(Milliseconds(32));

    auto released = Clock::now();

    lock.unlock();

    auto acquired = result.get();
    ASSERT_GT(acquired, released);
}

TEST_F(FUSESharedMutexTests, shared_lock_fails)
{
    SharedMutex mutex;

    UniqueLock<SharedMutex> lock(mutex, std::try_to_lock);
    ASSERT_TRUE(lock);

    auto result = execute(std::function<bool()>([&]() {
        return !SharedLock<SharedMutex>(mutex, std::try_to_lock);
    }));

    ASSERT_TRUE(result.get());

    ASSERT_FALSE(SharedLock<SharedMutex>(mutex, std::try_to_lock));
}

TEST_F(FUSESharedMutexTests, shared_lock_recursive_succeeds)
{
    SharedMutex mutex;

    SharedLock<SharedMutex> lock0(mutex, std::try_to_lock);
    ASSERT_TRUE(lock0);

    SharedLock<SharedMutex> lock1(mutex, std::try_to_lock);
    ASSERT_TRUE(lock1);
}

TEST_F(FUSESharedMutexTests, shared_lock_succeeds)
{
    SharedMutex mutex;

    SharedLock<SharedMutex> lock(mutex, std::try_to_lock);
    ASSERT_TRUE(lock);

    auto result = execute(std::function<TimePoint()>([&]() {
        SharedLock<SharedMutex> lock(mutex, std::try_to_lock);

        if (lock)
            return Clock::now();

        return TimePoint::max();
    }));

    std::this_thread::sleep_for(Milliseconds(32));

    auto acquired = result.get();

    ASSERT_LE(acquired, Clock::now());
}

TEST_F(FUSESharedMutexTests, to_shared_lock_succeeds)
{
    SharedMutex mutex;

    UniqueLock<SharedMutex> lock0(mutex, std::try_to_lock);
    ASSERT_TRUE(lock0);

    std::function<TimePoint()> acquire = [&mutex]() {
        SharedLock<SharedMutex> lock(mutex, std::defer_lock);

        if (lock.try_lock_for(Milliseconds(256)))
            return Clock::now();

        return TimePoint::min();
    }; // acquire

    auto result0 = execute(acquire);
    auto result1 = execute(acquire);

    std::this_thread::sleep_for(Milliseconds(32));

    auto released = Clock::now();

    auto lock1 = lock0.to_shared_lock();
    ASSERT_TRUE(lock1);

    ASSERT_FALSE(lock0);
    ASSERT_EQ(lock0.mutex(), lock1.mutex());
    ASSERT_EQ(lock0.mutex(), &mutex);

    auto acquired0 = result0.get();
    ASSERT_GT(acquired0, released);

    auto acquired1 = result1.get();
    ASSERT_GT(acquired1, released);
}

TEST_F(FUSESharedMutexTests, to_unique_lock_succeeds)
{
    SharedMutex mutex;

    SharedLock<SharedMutex> lock0(mutex, std::try_to_lock);
    ASSERT_TRUE(lock0);

    auto result = execute(std::function<TimePoint()>([&]() {
        SharedLock<SharedMutex> lock(mutex, std::try_to_lock);

        if (!lock)
            return TimePoint::min();

        std::this_thread::sleep_for(Milliseconds(32));

        auto released = Clock::now();

        lock.unlock();

        return released;
    }));

    auto lock1 = lock0.try_to_unique_lock_for(Milliseconds(64));
    ASSERT_TRUE(lock1);

    auto acquired = Clock::now();

    ASSERT_GT(acquired, result.get());

    ASSERT_FALSE(lock0);
    ASSERT_EQ(lock0.mutex(), lock1.mutex());
    ASSERT_EQ(lock0.mutex(), &mutex);
}

} // testing
} // fuse
} // mega

