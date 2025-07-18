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

class FileExplorerSetter::Executor: public common::TaskExecutor
{
    friend class FileExplorerSetter;

    bool mInitialized{false};

    void workerStarted(std::thread::id id) override;

    void workerStopped(std::thread::id id) override;

public:
    Executor();
};

// Note: The correctness based on the fact: We have one worker, running forever
FileExplorerSetter::Executor::Executor():
    common::TaskExecutor(
        []()
        {
            TaskExecutorFlags flags;
            flags.mMinWorkers = 1;
            flags.mMaxWorkers = 1;
            flags.mIdleTime = std::chrono::hours(876'000); // Run forever. About 100 years
            return flags;
        }(),
        logger())
{}

void FileExplorerSetter::Executor::workerStarted(std::thread::id)
{
    mInitialized = shell::init();
}

void FileExplorerSetter::Executor::workerStopped(std::thread::id)
{
    if (mInitialized)
        shell::uninit();
}

FileExplorerSetter::FileExplorerSetter():
    mExecutor(std::make_unique<FileExplorerSetter::Executor>())
{
    // start the worker
    mExecutor->execute([](const Task&) {}, false);
}

FileExplorerSetter::~FileExplorerSetter() = default;

void FileExplorerSetter::notify(std::function<shell::Prefixes()> getPrefixes)
{
    if (!mExecutor->mInitialized)
        return;

    mExecutor->execute(
        [getPrefixes = std::move(getPrefixes)](const Task& task)
        {
            if (task.cancelled())
                return;

            if (auto prefixes = getPrefixes(); !prefixes.empty())
            {
                shell::setView(prefixes);
            }
        },
        false);
}

} // platform
} // fuse
} // mega
