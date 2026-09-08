#pragma once

#include <mega/common/instance_logger.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_result_forward.h>
#include <mega/file_service/file_service_context_forward.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace mega
{
namespace file_service
{

class FileFlusher
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // Specifies when an individual file should be flushed.
    struct PendingFlush
    {
        PendingFlush(TimePoint requested, std::size_t retries):
            mRequested(requested),
            mScheduled(requested),
            mRetries(retries)
        {}

        // When should this file be flushed?
        TimePoint mRequested;

        // When is this file scheduled to be flushed?
        TimePoint mScheduled;

        // How many times have we tried to flush this file?
        std::size_t mRetries;
    }; // PendingFlush

    // Convenience.
    using PendingFlushMap = std::map<FileID, PendingFlush>;

    // Logs this instance's lifetime.
    common::InstanceLogger<FileFlusher> mInstanceLogger;

    // Used to wake our scheduler thread.
    std::condition_variable mCV;

    // Tracks which files are to be flushed and when.
    PendingFlushMap mPending;

    // Serializes access to our members.
    std::mutex mMutex;

    // Files to be flushed ordered by ascending flush time.
    std::deque<PendingFlushMap::iterator> mQueue;

    // The service whose files we are flushing.
    FileServiceContext& mService;

    // Tells our scheduler thread when its time to terminate.
    bool mTerminate;

    // The thread responsible for flushing files.
    std::thread mThread;

    // Called when it's time to flush a file to the cloud.
    void flush(FileID id, std::size_t retries);

    // Called when a file has been flushed.
    void flushed(File& file, FileResult result, std::size_t retries);

    // Our scheduler thread's entry point.
    void loop();

    // Schedule a file to be flushed after a given period of time.
    template<typename Period, typename Rep>
    void schedule(FileID id, std::size_t retries, std::chrono::duration<Rep, Period> period)
    {
        using std::chrono::duration_cast;

        // Capture current time.
        const auto now = Clock::now();

        // Compute maximum sane flush delay.
        const auto max = duration_cast<decltype(period)>(TimePoint::max() - now);

        // Schedule flush for at most max units in the future.
        schedule(id, retries, now + std::min(period, max));
    }

    // Schedule a file to be flushed at a given time.
    void schedule(FileID id, std::size_t retries, TimePoint when);

public:
    explicit FileFlusher(FileServiceContext& service);

    ~FileFlusher();

    // Cancel a file's scheduled flush.
    bool cancel(FileID id);

    // Cancel all scheduled flushes.
    bool cancel();

    // Schedule a file to be flushed after a given period of time.
    template<typename Period, typename Rep>
    void schedule(FileID id, std::chrono::duration<Rep, Period> period)
    {
        schedule(id, 0, period);
    }

    // Schedule a file to be flushed at a given time.
    void schedule(FileID id, TimePoint when)
    {
        schedule(id, 0, when);
    }
}; // FileFlusher

} // file_service
} // mega
