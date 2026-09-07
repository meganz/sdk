#pragma once

#include <mega/common/concurrent_execution_policy_forward.h>
#include <mega/common/task_executor.h>

#include <functional>
#include <type_traits>

namespace mega
{
namespace common
{

// This policy allows callers to execute callbacks on a thread pool.
class ConcurrentExecutionPolicy
{
    // The executor used to execute callbacks on a thread pool.
    TaskExecutor& mExecutor;

public:
    explicit ConcurrentExecutionPolicy(TaskExecutor& executor):
        mExecutor(executor)
    {}

    // Execute callback on the calling thread.
    template<typename Callback, typename... Parameters>
    auto dispatch(Callback&& callback, Parameters&&... arguments)
        -> std::enable_if_t<std::is_invocable_v<Callback, Parameters...>>
    {
        std::invoke(std::forward<Callback>(callback), std::forward<Parameters>(arguments)...);
    }

    // Execute callback on a thread potentially different from the calling thread.
    template<typename Callback, typename... Parameters>
    auto post(Callback&& callback, Parameters&&... arguments)
        -> std::enable_if_t<std::is_invocable_v<Callback, Parameters...>>
    {
        // Called to execute the caller's callback.
        auto wrapper = [](Parameters&... arguments, Callback& callback, const Task&)
        {
            std::invoke(std::move(callback), std::move(arguments)...);
        }; // wrapper

        // Queue the user's callback for execution.
        mExecutor.execute(std::bind(std::move(wrapper),
                                    std::forward<Parameters>(arguments)...,
                                    std::forward<Callback>(callback),
                                    std::placeholders::_1),
                          true);
    }
}; // ConcurrentExecutionPolicy

} // common
} // mega
