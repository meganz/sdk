#pragma once

#include <mega/common/task_executor_flags.h>
#include <mega/fuse/common/file_explorer_view.h>
#include <mega/fuse/common/inode_cache_flags.h>
#include <mega/fuse/common/service_flags_forward.h>
#include <mega/log_level.h>

#include <chrono>
#include <cstddef>

namespace mega
{
namespace fuse
{

struct ServiceFlags
{
    // How long should we wait before we flush after a write?
    std::chrono::seconds mFlushDelay = std::chrono::seconds(4);

    // Controls how the service caches inodes.
    InodeCacheFlags mInodeCacheFlags;

    // How verbose should FUSE's logs be?
    LogLevel mLogLevel = logInfo;

    // Controls what view the file explorer should have
    FileExplorerView mFileExplorerView = FILE_EXPLORER_VIEW_LIST;

    // Specifies how mounts should manage their worker threads.
    common::TaskExecutorFlags mMountExecutorFlags;

    // Specifies how the service should manage its worker threads.
    common::TaskExecutorFlags mServiceExecutorFlags;
}; // ServiceFlags

} // fuse
} // mega

