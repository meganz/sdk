#include <mega/common/task_executor.h>
#include <mega/common/task_executor_flags.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/file_explorer_setter.h>

#include <thread>

namespace mega
{
namespace fuse
{
namespace platform
{
using namespace common;

// One worker running forever
// This is required by FileExplorerSetter::Executor
static TaskExecutorFlags executorFlags()
{
    TaskExecutorFlags flags;
    flags.mMinWorkers = 1;
    flags.mMaxWorkers = 1;
    return flags;
}

class FileExplorerSetter::Executor: public common::TaskExecutor
{
    friend class FileExplorerSetter;

    bool mInitialized{false};

    void workerStarted(std::thread::id id) override;

    void workerStopped(std::thread::id id) override;

public:
    Executor();
};

FileExplorerSetter::Executor::Executor():
    common::TaskExecutor(executorFlags(), logger())
{}

void FileExplorerSetter::Executor::workerStarted(std::thread::id)
{
    mInitialized = shell::initialize();
}

void FileExplorerSetter::Executor::workerStopped(std::thread::id)
{
    if (mInitialized)
        shell::deinitialize();
}

FileExplorerSetter::FileExplorerSetter():
    mExecutor(std::make_unique<FileExplorerSetter::Executor>())
{
    // start the worker for workerStarted initialization
    mExecutor->execute([](const Task&) {}, false);
}

FileExplorerSetter::~FileExplorerSetter() = default;

void FileExplorerSetter::notify(std::function<shell::Prefixes()> getPrefixes)
{
    if (!mExecutor->mInitialized)
        return;

    auto setView = [getPrefixes = std::move(getPrefixes)](const Task& task)
    {
        if (task.cancelled())
            return;

        if (auto prefixes = getPrefixes(); !prefixes.empty())
        {
            shell::setView(prefixes);
        }
    };

    // There is a small chance that the notification is sent too early and File Explorer misses it.
    // In most cases, the first attempt will succeed.
    // The second attempt is scheduled after 30ms—a short enough delay to avoid visible UI flicker.
    // The third attempt is after 100ms as a final fallback, hoping it is long enough for the system
    // to be ready
    mExecutor->execute(setView, false);

    mExecutor->execute(setView, std::chrono::milliseconds{30}, false);

    mExecutor->execute(setView, std::chrono::milliseconds{100}, false);
}

} // platform
} // fuse
} // mega
