#pragma once

#include <functional>
#include <mutex>
#include <deque>

namespace mega
{
namespace gfx
{

class ThreadPool
{
public:
    using Entry = std::function<void()>;

    ThreadPool(size_t threadCount, size_t maxQueueSize);

    ThreadPool(const ThreadPool&) = delete;

    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool();

    bool push(Entry&& entry, const bool bypassMaxQueueSize = false);
private:
    void shutdown();

    void asyncThreadLoop();

    std::mutex mMutex;

    std::condition_variable mConditionVariable;

    size_t mMaxQueueSize;

    std::vector<std::thread> mThreads;

    // mQueue and mDone is condition should be protected with mMutex and notify by mConditionVariable
    std::deque<Entry> mQueue;

    bool mDone;
};

} // end of namespace
}