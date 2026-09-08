#include <mega/common/task_executor.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_flusher.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/logging.h>

#include <algorithm>
#include <cassert>

namespace mega
{
namespace file_service
{

struct Greater
{
    // Should the flush referenced by lhs be ordered before rhs?
    template<typename ConstIterator>
    bool operator()(ConstIterator lhs, ConstIterator rhs) const
    {
        return lhs->second.mScheduled > rhs->second.mScheduled;
    }
}; // Greater

// Convenience.
using namespace std::chrono;

void FileFlusher::flush(FileID id, std::size_t retries)
{
    // We've tried to flush this file too many times.
    if (retries >= mService.serviceOptions().mMaximumFileFlushRetries)
    {
        FSWarningF("Couldn't flush file: %s: Retried too many times", toString(id).c_str());
        return;
    }

    FSDebugF("Attempting to flush file: %s", toString(id).c_str());

    // Try and open the file.
    auto file = mService.open(id);

    // Couldn't open the file.
    if (!file)
    {
        FSErrorF("Couldn't flush file: %s: %s", toString(id).c_str(), toString(file.error()));
        return;
    }

    // Keeps the line below simple.
    auto flushed = std::bind(&FileFlusher::flushed, this, *file, std::placeholders::_1, retries);

    // Try and flush the file's modifications to the cloud.
    file->flush(std::move(flushed), true);
}

void FileFlusher::flushed(File& file, FileResult result, std::size_t retries)
{
    // Convenience.
    const auto id = file.info().id();

    // File was flushed to the cloud.
    if (result == FILE_SUCCESS)
    {
        FSDebugF("File flushed: %s", toString(id).c_str());
        return;
    }

    // Couldn't flush the file to the cloud.
    FSWarningF("Couldn't flush file: %s: %s", toString(id).c_str(), toString(result));

    // Don't retry if the flush was cancelled.
    if (result == FILE_CANCELLED)
        return;

    // Or if the file's been removed.
    if (result == FILE_REMOVED)
        return;

    // Grab a snapshot of the service's current options.
    const auto options = mService.serviceOptions();

    // We've tried to flush this file too many times.
    if (retries >= options.mMaximumFileFlushRetries)
        return;

    // Convenience.
    const auto delay = options.mFileFlushRetryBackoff * (1 << retries);

    FSInfoF("Will try and flush file %s again in %" PRIi64 "ms",
            toString(id).c_str(),
            duration_cast<milliseconds>(delay).count());

    // Retry flush with exponential backoff.
    schedule(id, retries + 1, delay);
}

void FileFlusher::loop()
{
    // Called to determine how long we should sleep for.
    auto nextWakeup = [this]()
    {
        // Sleep until the next scheduled flush.
        if (!mQueue.empty())
            return mQueue.front()->second.mScheduled;

        // Sleep forever.
        return steady_clock::time_point::max();
    }; // nextWake

    // Acquire lock.
    std::unique_lock lock(mMutex);

    while (!mTerminate)
    {
        // Sleep until we have something to do.
        mCV.wait_until(lock, nextWakeup());

        // Execute as many pending flushes as possible.
        while (!mQueue.empty() && !mTerminate)
        {
            // To avoid the scheduler waking too often.
            constexpr auto delta = std::chrono::milliseconds(1500);

            // So the current time is stable.
            const auto now = delta + Clock::now();

            // Pending flush is scheduled for some time in the future.
            if (mQueue.front()->second.mScheduled > now)
                break;

            // Pop the pending flush off the queue.
            auto iterator = std::move(mQueue.front());

            std::pop_heap(mQueue.begin(), mQueue.end(), Greater{});

            mQueue.pop_back();

            // Pending flush has been rescheduled.
            if (iterator->second.mRequested > now)
            {
                // Update scheduled flush time.
                iterator->second.mScheduled = iterator->second.mRequested;

                // Push the flush back into the queue.
                mQueue.emplace_back(iterator);

                // Make sure the queue's correctly ordered.
                std::push_heap(mQueue.begin(), mQueue.end(), Greater{});

                // Process the next pending flush.
                continue;
            }

            // Latch the ID of the file we need to flush.
            const auto id = iterator->first;

            // How many times has this flush been attempted?
            const auto retries = iterator->second.mRetries;

            // Flush is now in progress.
            mPending.erase(iterator);

            // Release lock.
            lock.unlock();

            // Flush the file's modifications to the cloud.
            flush(id, retries);

            // Reacquire lock.
            lock.lock();
        }
    }
}

void FileFlusher::schedule(FileID id, std::size_t retries, TimePoint when)
{
    // Sanity: id should never be undefined.
    assert(id);

    // Acquire lock.
    std::unique_lock lock(mMutex);

    // Determine when our timer will expire.
    auto expiryTime = [this]()
    {
        // Timer will expire when the earliest scheduled flush is due.
        if (!mQueue.empty())
            return mQueue.front()->second.mScheduled;

        // Timer will expire in the indefinite future.
        return TimePoint::max();
    }();

    // Reschedule the timer if necessary.
    auto rescheduleTimerIfNeeded = [&](auto when)
    {
        // Need to reschedule as timer will expire after when.
        if (expiryTime > when)
            mCV.notify_one();
    }; // maybeResheduleTimer

    // Try and add a pending flush for this file.
    auto [iterator, added] = mPending.emplace(std::piecewise_construct,
                                              std::forward_as_tuple(id),
                                              std::forward_as_tuple(when, retries));

    // Convenience.
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;

    FSDebugF("%s flush of file %s in %" PRIi64 "ms",
             added ? "Scheduling" : "Rescheduling",
             toString(id).c_str(),
             duration_cast<milliseconds>(when - steady_clock::now()).count());

    // File already had a pending flush.
    if (!added)
    {
        // Update the file's requested flush time.
        iterator->second.mRequested = when;

        // Update the file's retry count.
        iterator->second.mRetries = retries;

        // The file's flush time has been moved into the future.
        if (iterator->second.mScheduled < when)
            return;

        // Update the file's scheduled flush time.
        iterator->second.mScheduled = when;

        // Make sure this file's flush is ordered correctly.
        std::make_heap(mQueue.begin(), mQueue.end(), Greater{});

        // Reschedule the timer if necessary.
        return rescheduleTimerIfNeeded(when);
    }

    // File didn't have a flush pending.
    //
    // Add the file's pending flush to our queue.
    mQueue.emplace_back(iterator);

    // Make sure this file's flush is correctly ordered.
    std::push_heap(mQueue.begin(), mQueue.end(), Greater{});

    // Reschedule the timer if necessary.
    rescheduleTimerIfNeeded(when);
}

FileFlusher::FileFlusher(FileServiceContext& service):
    mInstanceLogger("FileFlusher", *this, logger()),
    mCV(),
    mPending(),
    mMutex(),
    mQueue(),
    mService(service),
    mTerminate(false),
    mThread(std::bind(&FileFlusher::loop, this))
{}

FileFlusher::~FileFlusher()
{
    // Let the scheduler thread know it's time to terminate.
    {
        std::unique_lock lock(mMutex);
        mTerminate = true;
    }

    // Wake the scheduler if necessary.
    mCV.notify_one();

    // Wait for our scheduler thread to terminate.
    mThread.join();
}

bool FileFlusher::cancel(FileID id)
{
    // Acquire lock.
    std::unique_lock lock(mMutex);

    // Check if a flush is scheduled for this file.
    auto iterator = mPending.find(id);

    // File has no flush scheduled or its flush has already completed.
    if (iterator == mPending.end())
        return false;

    // Remove the file's scheduled flush from our queue.
    mQueue.erase(std::remove(mQueue.begin(), mQueue.end(), iterator));

    // Make sure our queue's correctly ordered.
    std::make_heap(mQueue.begin(), mQueue.end(), Greater{});

    // File no longer has a pending flush.
    mPending.erase(iterator);

    // Let our caller know the file's flush has been cancelled.
    return true;
}

bool FileFlusher::cancel()
{
    // Acquire lock.
    std::unique_lock lock(mMutex);

    // Check whether any flushes are being cancelled.
    auto cancelled = !mPending.empty();

    // Clear pending flush map and queue.
    mPending.clear();
    mQueue.clear();

    // Let our caller know if any flushes were cancelled.
    return cancelled;
}

} // file_service
} // mega
