#include <cassert>

#include <chrono>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/shared_mutex.h>

namespace mega
{
namespace fuse
{

using std::chrono::steady_clock;

// Convenience.
using steady_time = steady_clock::time_point;

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

void SharedMutex::to_shared_lock()
{
    {
        std::lock_guard<std::mutex> guard(mLock);

        auto id = std::this_thread::get_id();

        // Make sure we currently own this mutex.
        assert(mWriterID == id);

        // Make sure we don't hold any recursive locks.
        assert(mCounter == -1);

        // Remember that we hold this lock.
        assert(++mReaders[id]);

        // Convert writer to reader.
        mCounter = 1;

        // No writers own this lock anymore.
        mWriterID = std::thread::id();

        // Silence compiler.
        static_cast<void>(id);
    }

    // Notify any sleeping readers.
    mReaderCV.notify_all();
}

void SharedMutex::to_unique_lock()
{
    while (!try_to_unique_lock_until(steady_time::max()))
        ;
}

bool SharedMutex::try_lock_shared()
{
    return try_lock_shared_until(steady_clock::now());
}

bool SharedMutex::try_lock_shared_until(steady_clock::time_point time)
{
    std::unique_lock<std::mutex> lock(mLock);

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

bool SharedMutex::try_lock()
{
    return try_lock_until(steady_clock::now());
}

bool SharedMutex::try_lock_until(steady_clock::time_point time)
{
    std::unique_lock<std::mutex> lock(mLock);

    // What thread wants to acquire this mutex?
    auto id = std::this_thread::get_id();

    // Wait for the mutex to be available.
    auto result = mWriterCV.wait_until(lock, time, [&]() {
        return mWriterID == id || !mCounter;
    });

    // Couldn't acquire the mutex.
    if (!result)
        return result;

    // Mutex has been acquired.
    mCounter--;
    mWriterID = id;

    return true;
}

bool SharedMutex::try_to_unique_lock_until(steady_clock::time_point time)
{
    std::unique_lock<std::mutex> lock(mLock);

    // Wait until a single reader owns this mutex.
    auto result = mReaderCV.wait_until(lock, time, [&]() {
        return mCounter == 1;
    });

    // Too many readers retain ownership of this mutex.
    if (!result)
        return false;

    auto id = std::this_thread::get_id();

    // Make sure a single reader owns this lock.
    assert(mReaders.size() == 1);

    // Make sure that reader is us.
    assert(mReaders.count(id));

    // And that there are no recursive locks.
    assert(mReaders[id] == 1);

    // Convert to exclusive ownership.
    mCounter = -1;
    mWriterID = id;

    // We no longer hold a read lock on this mutex.
    assert(mReaders.erase(id) == 1);

    return true;
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
        if (!(counter = ++mCounter))
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

    // Mutex is held by more than one reader.
    if (counter > 1)
        return;

    // Mutex is held by a single reader.
    if (counter > 0)
        return mReaderCV.notify_one();

    // Mutex is available.
    mWriterCV.notify_one();
}

} // fuse
} // mega

