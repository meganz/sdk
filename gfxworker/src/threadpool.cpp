#include "gfxworker/threadpool.h"
#include "mega/logging.h"

namespace mega
{
namespace gfx
{

bool ThreadPool::initialize(const size_t threadCount, const size_t maxQueueSize, const std::string& ownerName /*= std::string()*/)
{
    if (!mThreads.empty())
    {
        LOG_err << "Unable to initialize thread pool, already initialized";
        return false;
    }
    mMaxQueueSize = maxQueueSize;
    for (size_t i = threadCount; i--; )
    {
        try
        {
            mThreads.emplace_back([this]() mutable
                {
                    asyncThreadLoop();
                });
        }
        catch (std::system_error& e)
        {
            LOG_err << "Unable to initialize thread pool: " << e.what();
            return false;
        }
    }
    return true;
}

void ThreadPool::shutdown()
{
    push(nullptr);
    mConditionVariable.notify_all();
    for (auto& thread : mThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    mThreads.clear();
    mQueue.clear();
}

ThreadPool::~ThreadPool()
{
    LOG_verbose << "~ThreadPool";
    shutdown();
}

bool ThreadPool::push(Entry&& entry, const bool bypassMaxQueueSize/* = false*/)
{
    if (mThreads.empty())
    {
        if (entry)
        {
            entry();
        }
        return true;
    }
    {
        std::lock_guard<std::mutex> g(mMutex);
        if (!bypassMaxQueueSize && mMaxQueueSize > 0 && mQueue.size() >= mMaxQueueSize)
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
    for (;;)
    {
        Entry entry;
        {
            std::unique_lock<std::mutex> g(mMutex);
            mConditionVariable.wait(g, [this]() { return !mQueue.empty(); });
            entry = std::move(mQueue.front());
            if (!entry)
            {
                return; // nullptr is not popped, and causes all the threads to exit
            }
            mQueue.pop_front();
        }
        entry();
    }
}

} // end namespace gfx
} // end namespace mega