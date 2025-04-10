#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>

#include <mega/common/shared_mutex_forward.h>

namespace mega
{
namespace common
{

class SharedMutex
{
    // How many threads own this mutex?
    //
    // >0 One or more readers own this mutex.
    //  0 No one owns this mutex.
    // <0 A writer owns this mutex.
    std::int64_t mCounter = 0;

    // Serializes access to instance members.
    std::mutex mLock;

    // Used to wake potential readers.
    std::condition_variable mReaderCV;

#ifndef NDEBUG
    // What readers own this lock?
    std::map<std::thread::id, std::uint64_t> mReaders;
#endif // !NDEBUG
    
    // Used to wake potential writers.
    std::condition_variable mWriterCV;

    // What thread owns this mutex?
    std::thread::id mWriterID;

public:
    // Acquire shared ownership of this mutex.
    void lock_shared();

    // Acquire exclusive ownership of this mutex.
    void lock();

    // Try to acquire shared ownership of this mutex.
    bool try_lock_shared();

    // Try to acquire shared ownership of this mutex.
    template<typename Rep, typename Duration>
    bool try_shared_lock_for(std::chrono::duration<Rep, Duration> duration)
    {
        auto now = std::chrono::steady_clock::now();

        return try_lock_shared_until(now + duration);
    }

    // Try to acquire shared ownership of this mutex.
    bool try_lock_shared_until(std::chrono::steady_clock::time_point time);

    // Try to acquire exclusive ownership of this mutex.
    bool try_lock();

    // Try to acquire exclusive ownership of this mutex.
    template<typename Rep, typename Duration>
    bool try_lock_for(std::chrono::duration<Rep, Duration> duration)
    {
        auto now = std::chrono::steady_clock::now();

        return try_lock_until(now + duration);
    }

    // Try to acquire exclusive ownership of this mutex.
    bool try_lock_until(std::chrono::steady_clock::time_point time);

    // Release exclusive ownership of this mutex.
    void unlock();

    // Release shared ownership of this mutex.
    void unlock_shared();
}; // SharedMutex

} // common
} // mega

