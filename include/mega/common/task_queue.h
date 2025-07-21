#pragma once

#include <chrono>
#include <deque>
#include <functional>

#include <mega/common/logger_forward.h>
#include <mega/common/task_queue_forward.h>

namespace mega
{
namespace common
{

// Represents a task that has been queued for execution.
class Task
{
    // So the queue can access our context.
    friend class TaskQueue;

    // Describes our task.
    TaskContextPtr mContext;

public:
    Task() = default;

    // Create a task that is to run at some point in time.
    Task(std::function<void(const Task&)> function,
         Logger& logger,
         std::chrono::steady_clock::time_point when);

    // Create a task that is to run at some point in the future.
    template<typename Rep, typename Period>
    Task(std::function<void(const Task&)> function,
         Logger& logger,
         std::chrono::duration<Rep, Period> when)
      : Task(std::move(function),
             logger,
             std::chrono::steady_clock::now() + when)
    {
    }

    // Create a task that should run now.
    Task(std::function<void(const Task&)> function,
         Logger& logger)
      : Task(std::move(function),
             logger,
             std::chrono::steady_clock::now())
    {
    }

    Task(const Task& other) = default;

    Task(Task&& other) = default;

    Task& operator=(const Task& rhs) = default;

    Task& operator=(Task&& rhs) = default;

    // True if this instance references a task.
    operator bool() const;

    // True if this instance does not reference a task.
    bool operator!() const;

    // Try and abort the task.
    bool abort();

    // Try and cancel the task.
    bool cancel();

    // Has the task been aborted?
    bool aborted() const;

    // Has the task been cancelled?
    bool cancelled() const;

    // Try and complete the task.
    bool complete();

    // Has the task been completed?
    bool completed() const;

    // Detach ourselves from our referenced task.
    void reset();
}; // Task

class TaskQueue
{
    // True if lhs is due earlier than rhs.
    static bool earlier(const Task& lhs, const Task& rhs);

    // Tracks what tasks have been queued.
    std::deque<Task> mTasks;

public:
    TaskQueue();

    TaskQueue(const TaskQueue& other) = delete;

    ~TaskQueue();

    TaskQueue& operator=(const TaskQueue& rhs) = delete;

    // Dequeue a number of tasks.
    void dequeue(std::deque<Task>& tasks, std::size_t count);

    // Dequeue a task.
    Task dequeue();

    // Have any tasks been queued?
    bool empty() const;

    // Queue a task for execution.
    Task queue(Task task);

    // Is a task ready for execution?
    bool ready() const;

    // When will the next task be ready for execution?
    std::chrono::steady_clock::time_point when() const;
}; // TaskQueue

} // common
} // mega

