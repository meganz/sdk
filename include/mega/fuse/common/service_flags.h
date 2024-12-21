#pragma once

#include <cstddef>
#include <chrono>

#include <mega/fuse/common/inode_cache_flags.h>
#include <mega/fuse/common/log_level.h>
#include <mega/fuse/common/service_flags_forward.h>
#include <mega/fuse/common/task_executor_flags.h>

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
    LogLevel mLogLevel = LOG_LEVEL_INFO;

    // Specifies how mounts should manage their worker threads.
    TaskExecutorFlags mMountExecutorFlags;

    // Specifies how the service should manage its worker threads.
    TaskExecutorFlags mServiceExecutorFlags;
}; // ServiceFlags

} // fuse
} // mega

