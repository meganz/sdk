#include <mega/common/logging.h>
#include <mega/common/testing/watchdog.h>

#include <cstdlib>

namespace mega
{
namespace common
{
namespace testing
{

Watchdog::Watchdog(Logger& logger):
    mLogger(logger),
    mExecutor(
        []()
        {
            TaskExecutorFlags flags;
            flags.mMaxWorkers = 1;
            return flags;
        }(),
        mLogger),
    mTask()
{}

void Watchdog::arm(std::chrono::steady_clock::time_point when)
{
    // Disarm the watchdog.
    disarm();

    // Re-arm the watchdog.
    mTask = mExecutor.execute(
        [this](const Task& task)
        {
            // Watchdog's being torn down.
            if (task.cancelled())
                return;

            // Log a friendly message.
            Log1(mLogger, "Watchdog timed out", logError);

            // Kill the program.
            std::abort();
        },
        when,
        true);
}

void Watchdog::disarm()
{
    // Disarm the watchdog.
    mTask.cancel();
    mTask.reset();
}

} // testing
} // common
} // mega
