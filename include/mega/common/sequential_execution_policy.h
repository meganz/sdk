#pragma once

#include <mega/common/sequential_execution_policy_forward.h>

#include <functional>
#include <type_traits>

namespace mega
{
namespace common
{

// This policy always executes callbacks on the calling thread.
struct SequentialExecutionPolicy
{
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
        std::invoke(std::forward<Callback>(callback), std::forward<Parameters>(arguments)...);
    }
}; // SequentialExecutionPolicy

} // common
} // mega
