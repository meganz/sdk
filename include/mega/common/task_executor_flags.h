#pragma once

#include <cstddef>
#include <chrono>

#include <mega/common/task_executor_flags_forward.h>

namespace mega
{
namespace common
{

struct TaskExecutorFlags
{
    // How long should a worker stay idle before it quits?
    std::chrono::seconds mIdleTime = std::chrono::seconds(16);

    // Maximum number of worker threads.
    std::size_t mMaxWorkers = 16;

    // Minimum number of worker threads.
    std::size_t mMinWorkers = 0;
}; // TaskExecutorFlags

} // common
} // mega

