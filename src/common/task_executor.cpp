#include <cassert>
#include <chrono>
#include <thread>

#include <mega/common/logging.h>
#include <mega/common/task_executor.h>
#include <mega/common/utility.h>

#include <mega/types.h>

namespace mega
{
namespace common
{

class TaskExecutor::Worker
{
    // Executes tasks when appropriate.
    void loop();

    // Which executor hired us?
    TaskExecutor& mExecutor;

    // What logger should we use?
    Logger& mLogger;

    // Where are we in the executor's list of workers?
    WorkerList::iterator mPosition;

    // Where do we do our processing?
    std::thread mThread;

public:
    Worker(TaskExecutor& executor,
           Logger& logger,
           WorkerList::iterator position);

    ~Worker();
}; // Worker

TaskExecutor::TaskExecutor(const TaskExecutorFlags& flags,
                           Logger& logger)
  : mAvailableWorkers(0u)
  , mCV()
  , mFlags(flags)
  , mLock()
  , mLogger(logger)
  , mTaskQueue()
  , mTerminating(false)
  , mWorkers()
{
    LogDebug1(mLogger, "Executor constructed");
}

TaskExecutor::~TaskExecutor()
{
    // Acquire executor lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Let the workers know that it's time to call it quits.
    mTerminating = true;

    // Wake up all the workers.
    mCV.notify_all();

    // Wait for the workers to quit.
    while (!mWorkers.empty())
    {
        // Select a worker to wait on.
        auto worker = std::move(mWorkers.front());

        // Remove worker from the list.
        mWorkers.pop_front();

        // Release the lock.
        lock.unlock();

        // Wait for the worker to quit.
        worker.reset();

        // Reacquire lock.
        lock.lock();
    }

    LogDebug1(mLogger, "Executor destroyed");
}

Task TaskExecutor::execute(std::function<void(const Task&)> function,
                           std::chrono::steady_clock::time_point when,
                           bool spawnWorker)
{
    // Sanity.
    assert(function);

    // Instantiate a new task.
    auto task = Task(std::move(function), mLogger, when);

    // Acquire executor lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Executor's being terminated.
    if (mTerminating)
    {
        // Release the lock.
        lock.unlock();

        // Cancel the task.
        task.cancel();

        // Return task to caller.
        return task;
    }

    // Only spawn a new worker if requested and if none are available.
    spawnWorker = spawnWorker && !mAvailableWorkers;

    // Always spawn a worker if there are none present.
    spawnWorker |= mWorkers.empty();

    // But only spawn so many.
    spawnWorker &= mWorkers.size() < mFlags.mMaxWorkers;

    // Spawn a new worker if necessary.
    if (spawnWorker)
    {
        // Allocate a position for the worker.
        auto position = mWorkers.emplace(mWorkers.end(), nullptr);

        // Instantiate the worker.
        *position = std::make_unique<Worker>(*this, mLogger, position);

        // We now have at least one worker available.
        ++mAvailableWorkers;
    }

    // Sanity.
    assert(!mWorkers.empty());

    // Queue the task for execution.
    mTaskQueue.queue(task);

    // Release executor lock.
    lock.unlock();

    // Let a worker know there's something to do.
    mCV.notify_one();

    // Return task to caller.
    return task;
}

void TaskExecutor::flags(const TaskExecutorFlags& flags)
{
    std::lock_guard<std::mutex> guard(mLock);

    mFlags = flags;

    mCV.notify_all();
}

TaskExecutorFlags TaskExecutor::flags() const
{
    std::lock_guard<std::mutex> guard(mLock);

    return mFlags;
}

void TaskExecutor::Worker::loop()
{
    // Acquire executor lock.
    std::unique_lock<std::mutex> lock(mExecutor.mLock);

    // Convenience.
    auto& availableWorkers = mExecutor.mAvailableWorkers;
    auto& cv = mExecutor.mCV;
    auto& flags = mExecutor.mFlags;
    auto& taskQueue = mExecutor.mTaskQueue;
    auto& terminating = mExecutor.mTerminating;
    auto& workers = mExecutor.mWorkers;

    // When should we wake up?
    auto nextWakeup = [&]() {
        if (!taskQueue.empty())
            return taskQueue.when();

        using std::chrono::steady_clock;

        return steady_clock::now() + flags.mIdleTime;
    }; // nextWakeup

    // Should we wake up?
    auto shouldWake = [&]() {
        return terminating || taskQueue.ready();
    }; // shouldWake

    auto threadId = std::this_thread::get_id();
    mExecutor.workerStarted(threadId);

    LogDebug1(mLogger, "Worker thread started");

    // Execute queued tasks.
    while (true)
    {
        // Release excess workers.
        if (workers.size() > flags.mMaxWorkers)
            break;

        // Sleep until there's something to do.
        auto hasWork = cv.wait_until(lock, nextWakeup(), shouldWake);

        // We haven't had any work in awhile.
        if (!hasWork)
        {
            // Keep at least this many workers alive.
            if (flags.mMinWorkers >= workers.size())
                continue;

            // Keep at least a single worker alive if there tasks pending.
            if (!taskQueue.empty() && workers.size() < 2)
                continue;

            // So we don't block on our own removal.
            mThread.detach();

            // Leave a trail of what's going on.
            LogDebug1(mLogger, "Worker thread stopped");

            // Let the executor know it has one less worker.
            --availableWorkers;

            workers.erase(mPosition);

            // We're all done.
            return;
        }

        // Executor's closing up shop.
        if (mExecutor.mTerminating)
            break;

        // Pop a task from the queue.
        auto task = taskQueue.dequeue();

        // Sanity.
        assert(task);

        // Let the executor know we're busy.
        --availableWorkers;

        // Release the lock so other workers can proceed.
        lock.unlock();

        // Complete the task.
        task.complete();

        // Reacquire lock.
        lock.lock();

        // Let the executor know we're available.
        ++availableWorkers;
    }

    // Let the executor know it has one less worker.
    --availableWorkers;

    mExecutor.workerStopped(threadId);

    LogDebug1(mLogger, "Worker thread stopped");
}

TaskExecutor::Worker::Worker(TaskExecutor& executor,
                             Logger& logger,
                             WorkerList::iterator position)
  : mExecutor(executor)
  , mLogger(logger)
  , mPosition(position)
  , mThread(&Worker::loop, this)
{
    LogDebug1(mLogger, "Worker constructed");
}

TaskExecutor::Worker::~Worker()
{
    if (mThread.joinable())
        mThread.join();

    LogDebug1(mLogger, "Worker destroyed");
}

} // common
} // mega

