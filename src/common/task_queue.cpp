#include <algorithm>
#include <atomic>
#include <cassert>
#include <stdexcept>

#include <mega/common/logging.h>
#include <mega/common/task_queue.h>

namespace mega
{
namespace common
{

class TaskContext
{
    enum StatusFlag : unsigned int
    {
        // The task can be cancelled.
        SF_CANCELLABLE = 1,
        // The task has been cancelled.
        SF_CANCELLED = 2,
        // The task has been executed.
        SF_COMPLETED = 4
    }; // StatusFlag

    using StatusFlags = unsigned int;

    // Execute mFunction if the task has not already completed.
    bool execute(StatusFlags desired, const Task& task);

    // What will the task do when executed?
    std::function<void(const Task&)> mFunction;

    // How should this task log exceptions?
    Logger& mLogger;

    // What is the task's status?
    std::atomic<StatusFlags> mStatus;

    // When is the task to be executed?
    std::chrono::steady_clock::time_point mWhen;

public:
    TaskContext(std::function<void(const Task&)> function,
                Logger& logger,
                std::chrono::steady_clock::time_point when);

    // Orders by ascending deadline.
    bool operator<(const TaskContext& rhs) const;

    // Try and cancel the task.
    bool cancel(const Task& task);

    // Has the task been cancelled?
    bool cancelled() const;

    // Try and complete the task.
    bool complete(const Task& task);

    // Has the task been completed?
    bool completed() const;

    // When is the task due to execution?
    std::chrono::steady_clock::time_point when() const;
}; // TaskContext

Task::Task(std::function<void(const Task&)> function,
           Logger& logger,
           std::chrono::steady_clock::time_point when)
  : mContext(std::make_shared<TaskContext>(std::move(function),
                                           logger,
                                           when))
{
}

Task::operator bool() const
{
    return !!mContext;
}

bool Task::operator!() const
{
    return !mContext;
}

bool Task::cancel()
{
    if (mContext)
        return mContext->cancel(*this);

    return false;
}

bool Task::cancelled() const
{
    if (mContext)
        return mContext->cancelled();

    return false;
}

bool Task::complete()
{
    if (mContext)
        return mContext->complete(*this);

    return false;
}

bool Task::completed() const
{
    if (mContext)
        return mContext->completed();

    return false;
}

void Task::reset()
{
    mContext.reset();
}

bool TaskQueue::earlier(const Task& lhs, const Task& rhs)
{
    return *lhs.mContext < *rhs.mContext;
}

TaskQueue::TaskQueue()
  : mTasks()
{
}

TaskQueue::~TaskQueue()
{
    // Cancel any outstanding tasks.
    while (!mTasks.empty())
    {
        mTasks.back().cancel();
        mTasks.pop_back();
    }
}

void TaskQueue::dequeue(std::deque<Task>& tasks, std::size_t count)
{
    using std::swap;

    // Caller wants all tasks.
    if (count >= mTasks.size())
        return swap(mTasks, tasks), mTasks.clear();

    // Caller only wants so many tasks.
    while (count-- && !mTasks.empty())
    {
        // Pop the next due task from the queue.
        std::pop_heap(mTasks.begin(), mTasks.end(), earlier);

        auto task = std::move(mTasks.back());

        mTasks.pop_back();

        // Transfer ownership of task to caller.
        tasks.emplace_back(std::move(task));
    }
}

Task TaskQueue::dequeue()
{
    // No tasks have been queued.
    if (mTasks.empty())
        return Task();

    // Pop the next due task from the queue.
    std::pop_heap(mTasks.begin(), mTasks.end(), earlier);

    auto task = std::move(mTasks.back());

    mTasks.pop_back();

    // Return task to caller.
    return task;
}

bool TaskQueue::empty() const
{
    return mTasks.empty();
}

Task TaskQueue::queue(Task task)
{
    // Task doesn't reference anything.
    if (!task)
        return task;

    // Task's already been completed.
    if (task.completed())
        return task;

    // Queue the task for execution.
    mTasks.emplace_back(task);

    // Order tasks by ascending due time.
    std::push_heap(mTasks.begin(), mTasks.end(), earlier);

    // Return task to caller.
    return task;
}

bool TaskQueue::ready() const
{
    return std::chrono::steady_clock::now() >= when();
}

std::chrono::steady_clock::time_point TaskQueue::when() const
{
    if (mTasks.empty())
        return std::chrono::steady_clock::time_point::max();

    return mTasks.front().mContext->when();
}

bool TaskContext::execute(StatusFlags desired, const Task& task)
{
    StatusFlags expected = SF_CANCELLABLE;

    // Task's already been completed.
    if (!mStatus.compare_exchange_strong(expected, desired))
        return false;

    // Safely execute the task.
    try
    {
        mFunction(task);
    }
    catch (std::exception& exception)
    {
        LogErrorF(mLogger,
                  "Exception encountered executing task: %s",
                  exception.what());
    }

    // Release the closure.
    mFunction = nullptr;

    // Task's been executed.
    return true;
}

TaskContext::TaskContext(std::function<void(const Task&)> function,
                         Logger& logger,
                         std::chrono::steady_clock::time_point when):
    mFunction(std::move(function)),
    mLogger(logger),
    mStatus{SF_CANCELLABLE},
    mWhen(when)
{}

bool TaskContext::operator<(const TaskContext& rhs) const
{
    return mWhen > rhs.mWhen;
}

bool TaskContext::cancel(const Task& task)
{
    return execute(SF_CANCELLED | SF_COMPLETED, task);
}

bool TaskContext::cancelled() const
{
    return (mStatus & SF_CANCELLED);
}

bool TaskContext::complete(const Task& task)
{
    return execute(SF_COMPLETED, task);
}

bool TaskContext::completed() const
{
    return (mStatus & SF_COMPLETED);
}

std::chrono::steady_clock::time_point TaskContext::when() const
{
    return mWhen;
}

bool compare(const TaskContextPtr& lhs, const TaskContextPtr& rhs)
{
    return *lhs < *rhs;
}

} // common
} // mega

