#include <cstdlib>

#include <mega/fuse/common/testing/watchdog.h>
#include <mega/fuse/common/logging.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using common::Task;
using common::TaskExecutorFlags;

Watchdog::Watchdog()
  : mExecutor([]() {
        TaskExecutorFlags flags;
        flags.mMaxWorkers = 1;
        return flags;
    }(), logger())
{
}

void Watchdog::arm(std::chrono::steady_clock::time_point when)
{
    // Disarm the watchdog.
    disarm();

    // Re-arm the watchdog.
    mTask = mExecutor.execute([](const Task& task) {
        // Watchdog's being torn down.
        if (task.cancelled())
            return;

        // Log a friendly message.
        FUSEError1("Watchdog timed out");

        // Kill the program.
        std::abort();
    }, when, true);
}

void Watchdog::disarm()
{
    // Disarm the watchdog.
    mTask.cancel();
    mTask.reset();
}

} // testing
} // fuse
} // mega

