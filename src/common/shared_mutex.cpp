#include <cassert>
#include <chrono>

#include <mega/common/logging.h>
#include <mega/common/shared_mutex.h>
#include <mega/common/shared_mutex.h>

namespace mega
{
namespace common
{

using std::chrono::steady_clock;

// Convenience.
using steady_time = steady_clock::time_point;

bool SharedMutex::try_lock_shared_until(steady_clock::time_point time,
                                        [[maybe_unused]] bool validate)
{
    std::unique_lock<std::mutex> lock(mLock);

    // What thread is trying to acquire this mutex?
    auto id = std::this_thread::get_id();

    // Make sure the thread doesn't already hold a write lock.
    assert(!validate || mWriterID != id);

    // Wait for the mutex to be available.
    auto result = mReaderCV.wait_until(lock, time, [&]() {
        return mCounter >= 0;
    });

    // Couldn't acquire the mutex.
    if (!result)
        return false;

    // Remember that we're holding this lock.
    assert((++mReaders[std::this_thread::get_id()], true));

    // We've acquired the mutex.
    ++mCounter;

    return true;
}

bool SharedMutex::try_lock_until(steady_clock::time_point time,
                                 [[maybe_unused]] bool validate)
{
    std::unique_lock<std::mutex> lock(mLock);

    // What thread wants to acquire this mutex?
    auto id = std::this_thread::get_id();

    // Make sure this thread doesn't already hold a read lock.
    assert(!validate || !mReaders.count(id) || !mReaders[id]);

    // Make sure this thread doesn't already hold a write lock.
    assert(!validate || id != mWriterID);

    // Wait for the mutex to be available.
    auto result = mWriterCV.wait_until(lock, time, [&]() {
        return !mCounter;
    });

    // Couldn't acquire the mutex.
    if (!result)
        return result;

    // Mutex has been acquired.
    mCounter--;
    mWriterID = id;

    return true;
}

void SharedMutex::lock_shared()
{
    while (!try_lock_shared_until(steady_time::max()))
        ;
}

void SharedMutex::lock()
{
    while (!try_lock_until(steady_time::max()))
        ;
}

bool SharedMutex::try_lock_shared()
{
    return try_lock_shared_until(steady_clock::now());
}

bool SharedMutex::try_lock()
{
    return try_lock_until(steady_clock::now());
}

void SharedMutex::unlock()
{
    std::int64_t counter;

    {
        std::lock_guard<std::mutex> guard(mLock);

        // Make sure the lock is held.
        assert(mCounter < 0);

        // And that we own this mutex.
        assert(mWriterID == std::this_thread::get_id());

        // Release the mutex.
        counter = ++mCounter;
        if (!counter)
            mWriterID = std::thread::id();
    }

    // Mutex isn't available.
    if (counter < 0)
        return;

    // Notify waiting readers.
    mReaderCV.notify_one();

    // Notify waiting writers.
    mWriterCV.notify_one();
}

void SharedMutex::unlock_shared()
{
    std::int64_t counter;

    {
        std::lock_guard<std::mutex> guard(mLock);

        // Make sure the lock is held.
        assert(mCounter > 0);

        // And that we own this mutex.
        auto id = std::this_thread::get_id();

        // Make sure we know about this thread.
        assert(mReaders.count(id));

        // Verify this thread actually owns this mutex.
        assert(mReaders[id]-- > 0);

        // Remove thread from our set of readers if necessary.
        assert((!mReaders[id] && mReaders.erase(id)) || true);

        // Release the mutex.
        counter = --mCounter;

        // Silence compiler.
        static_cast<void>(id);
    }

    // Mutex is held by one or more readers.
    if (counter > 0)
        return;

    // Mutex is available.
    mWriterCV.notify_one();
}

} // common
} // mega

