#include "threadpool.h"

#include "mega/logging.h"

#include <algorithm>
namespace mega
{
namespace gfx
{

ThreadPool::ThreadPool(size_t threadCount, size_t maxQueueSize)
    : mMaxQueueSize(std::max<size_t>(maxQueueSize, 1)) // minimum 1
{
    threadCount = std::max<size_t>(threadCount, 1); // minimum 1
    for (size_t i = threadCount; i--; )
    {
        mThreads.emplace_back([this]() mutable { asyncThreadLoop(); });
    }
}

void ThreadPool::shutdown()
{
    {
        std::lock_guard<std::mutex> g(mMutex);
        mDone = true;
    }

    mConditionVariable.notify_all();

    for (auto& thread : mThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

ThreadPool::~ThreadPool()
{
    LOG_verbose << "~ThreadPool";
    shutdown();
}

bool ThreadPool::push(Entry&& entry)
{
    {
        std::lock_guard<std::mutex> g(mMutex);

        if (mDone) return false;

        if (mMaxQueueSize > 0 && mQueue.size() >= mMaxQueueSize)
        {
            return false;
        }
        mQueue.emplace_back(std::move(entry));
    }

    mConditionVariable.notify_one();
    return true;
}

void ThreadPool::asyncThreadLoop()
{
    for(;;)
    {
        Entry entry;
        {
            std::unique_lock<std::mutex> g(mMutex);

            mConditionVariable.wait(g, [this]() { return !mQueue.empty() || mDone; });

             // due to shutdown
            if (mDone) return;

            // due to job
            if (!mQueue.empty())
            {
                entry = std::move(mQueue.front());
                mQueue.pop_front();
            }
        }
        if (entry) entry();
    }
}

} // end namespace gfx
} // end namespace mega