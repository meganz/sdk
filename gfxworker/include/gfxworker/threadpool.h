#pragma once

#include <functional>
#include <mutex>
#include <deque>

namespace gfx
{

class ThreadPool
{
public:
    bool initialize(const size_t threadCount, const size_t maxQueueSize = 0, const std::string& ownerName = std::string());
    void shutdown();
    ~ThreadPool();

    using Entry = std::function<void()>;
    bool push(Entry&& entry, const bool bypassMaxQueueSize = false);
private:
    std::mutex mMutex;
    std::condition_variable mConditionVariable;
    size_t mMaxQueueSize;
    std::deque<Entry> mQueue;
    std::vector<std::thread> mThreads;
    void asyncThreadLoop();
};

}

