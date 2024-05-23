#pragma once

#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>

#include <mega/fuse/common/task_executor_flags.h>
#include <mega/fuse/common/task_executor.h>
#include <mega/fuse/common/task_queue.h>

namespace mega
{
namespace fuse
{

class TaskExecutor
{
    // Executes queued tasks when appropriate.
    class Worker;

    // Convenience.
    using WorkerPtr = std::unique_ptr<Worker>;
    using WorkerList = std::list<WorkerPtr>;

    // Tracks how many workers are waiting for work.
    std::size_t mAvailableWorkers;

    // Signalled when we want our worker's attention.
    std::condition_variable mCV;

    // Controls how we spawn our workers and how they behave.
    TaskExecutorFlags mFlags;

    // Serializes access to instance members.
    mutable std::mutex mLock;

    // Tracks what tasks we've queued.
    TaskQueue mTaskQueue;

    // Lets the workers know when they should terminate.
    bool mTerminating;

    // Tracks who our workers are.
    WorkerList mWorkers;

public:
    explicit TaskExecutor(const TaskExecutorFlags& flags);

    ~TaskExecutor();

    // Execute a task at some point in time.
    Task execute(std::function<void(const Task&)> function,
                 std::chrono::steady_clock::time_point when,
                 bool spawnWorker);

    // Execute a task at some point in the future.
    template<typename Rep, typename Period>
    Task execute(std::function<void(const Task&)> function,
                 std::chrono::duration<Rep, Period> when,
                 bool spawnWorker)
    {
        return execute(std::move(function),
                       std::chrono::steady_clock::now() + when,
                       spawnWorker);
    }

    // Execute a task now.
    Task execute(std::function<void(const Task&)> function,
                 bool spawnWorker)
    {
        return execute(std::move(function),
                       std::chrono::steady_clock::now(),
                       spawnWorker);
    }

    // Update this executor's flags.
    void flags(const TaskExecutorFlags& flags);

    // Retrieve this executor's flags.
    TaskExecutorFlags flags() const;
}; // TaskExecutor

} // fuse
} // mega

