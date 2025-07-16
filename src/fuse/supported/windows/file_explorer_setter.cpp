#include <mega/common/task_executor.h>
#include <mega/common/task_executor_flags.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/file_explorer_setter.h>
#include <mega/fuse/platform/shell.h>

#include <thread>

namespace mega
{
namespace fuse
{
namespace platform
{
using namespace common;

class FEViewSetter::Executor: public common::TaskExecutor
{
    friend class FEViewSetter;

    bool mInited{false};

    void workerStarted(std::thread::id id) override;

    void workerStopped(std::thread::id id) override;

public:
    Executor();
};

// Note: The correctness based on the fact: We have one worker, running forever
FEViewSetter::Executor::Executor():
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

void FEViewSetter::Executor::workerStarted(std::thread::id)
{
    mInited = shell::init();
}

void FEViewSetter::Executor::workerStopped(std::thread::id)
{
    if (mInited)
        shell::uninit();
}

FEViewSetter::FEViewSetter():
    mExecutor(std::make_unique<FEViewSetter::Executor>())
{
    // start the worker
    mExecutor->execute([](const Task&) {}, false);
}

FEViewSetter::~FEViewSetter() = default;

void FEViewSetter::notify(const std::wstring& prefix)
{
    if (!mExecutor->mInited)
        return;

    mExecutor->execute(
        [prefix](const Task& task)
        {
            if (task.cancelled())
                return;

            shell::setView(prefix);
        },
        false);
}

} // platform
} // fuse
} // mega
